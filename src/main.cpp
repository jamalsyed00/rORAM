#include "roram/roram.hpp"
#include "roram/path_oram.hpp"
#include "roram/types.hpp"
#include "roram/crypto.hpp"
#include "roram/storage.hpp"
#include <iostream>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <tuple>

static void usage(const char* prog) {
  std::cerr << "Usage: " << prog << " <init|read|write|bench|compare|workload> [options]\n"
            << "  init N L [Z] [B]     - init params (N blocks, L max range, Z bucket size, B block bytes)\n"
            << "  read N L a r         - read range [a, a+r) (params N, L)\n"
            << "  write N L a r        - write range [a, a+r) with zeros (params N, L)\n"
            << "  bench N L [trials]   - benchmark range sizes (default 5 trials)\n"
            << "  compare [--N N] [--L L] [--trials T] [--csv path] [--file path] [--seek-penalty-us N]\n"
            << "          - rORAM vs Path ORAM; use --seek-penalty-us to simulate seek cost (crossover)\n"
            << "  workload [--mode sequential|fileserver|videoserver] [--queries Q] [--N N] [--L L]\n"
            << "           [--seed S] [--seek-penalty-us N] [--file path] [--csv path]\n"
            << "          - trace-driven synchronous throughput benchmark (queries/sec and MB/s)\n";
}

// Path ORAM: range read as r sequential Access(addr, "read"). Returns total time in ms.
static double path_oram_range_read_ms(roram::PathORAM& ram, uint64_t a, uint64_t r) {
  auto start = std::chrono::high_resolution_clock::now();
  for (uint64_t i = 0; i < r; ++i)
    ram.Access(a + i, "read");
  auto end = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

static double path_oram_range_write_ms(roram::PathORAM& ram, uint64_t a, uint64_t r,
                                       const std::vector<std::vector<uint8_t>>& data) {
  auto start = std::chrono::high_resolution_clock::now();
  for (uint64_t i = 0; i < r; ++i) {
    ram.Access(a + i, "write", &data[static_cast<size_t>(i)]);
  }
  auto end = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

struct QueryOp {
  uint64_t a;
  uint64_t r;
  bool is_write;
};

static uint64_t lcg_next(uint64_t& state) {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return state;
}

static uint64_t clamp_range(uint64_t r, uint64_t L, uint64_t N) {
  uint64_t out = std::max<uint64_t>(1, r);
  if (L > 0) out = std::min(out, L);
  if (N > 0) out = std::min(out, N);
  return out;
}

static std::vector<QueryOp> make_workload_trace(uint64_t N, uint64_t L, uint64_t queries,
                                                const std::string& mode, uint64_t seed) {
  std::vector<QueryOp> trace;
  trace.reserve(static_cast<size_t>(queries));
  if (N == 0) return trace;

  uint64_t cursor = 0;
  auto next_rand = [&]() { return lcg_next(seed); };
  for (uint64_t i = 0; i < queries; ++i) {
    QueryOp q{0, 1, false};
    uint64_t x = next_rand();
    if (mode == "videoserver") {
      static const uint64_t sizes[] = {64, 128, 256, 512};
      q.r = clamp_range(sizes[x % 4], L, N);
      q.is_write = false;
      if (cursor + q.r > N) cursor = 0;
      q.a = cursor;
      cursor += q.r;
    } else if (mode == "fileserver") {
      static const uint64_t sizes[] = {1, 2, 4, 8, 16, 32, 64};
      q.r = clamp_range(sizes[x % 7], L, N);
      q.is_write = ((x >> 8) % 10) < 3;  // ~30% writes
      if (((x >> 12) % 10) < 7) {
        if (cursor + q.r > N) cursor = 0;
        q.a = cursor;
        cursor += q.r;
      } else {
        uint64_t max_start = (N > q.r) ? (N - q.r) : 0;
        q.a = max_start > 0 ? (next_rand() % max_start) : 0;
      }
    } else {  // sequential
      static const uint64_t sizes[] = {8, 16, 32, 64, 128};
      q.r = clamp_range(sizes[x % 5], L, N);
      q.is_write = ((x >> 10) % 20) == 0;  // rare writes (~5%)
      if (((x >> 14) % 10) < 8) {
        if (cursor + q.r > N) cursor = 0;
        q.a = cursor;
        cursor += q.r;
      } else {
        uint64_t max_start = (N > q.r) ? (N - q.r) : 0;
        q.a = max_start > 0 ? (next_rand() % max_start) : 0;
      }
    }
    trace.push_back(q);
  }
  return trace;
}

static void mean_std_ci(const std::vector<double>& samples, double& mean, double& std_dev, double& ci_low, double& ci_high) {
  const size_t n = samples.size();
  if (n == 0) { mean = std_dev = ci_low = ci_high = 0; return; }
  mean = 0;
  for (double x : samples) mean += x;
  mean /= n;
  double var = 0;
  for (double x : samples) var += (x - mean) * (x - mean);
  std_dev = (n > 1) ? std::sqrt(var / (n - 1)) : 0;
  const double ci_half = (n > 0) ? (1.96 * std_dev / std::sqrt(static_cast<double>(n))) : 0;
  ci_low = mean - ci_half;
  ci_high = mean + ci_half;
}

static double percentile(std::vector<double> samples, double p) {
  if (samples.empty()) return 0.0;
  if (p <= 0.0) p = 0.0;
  if (p >= 1.0) p = 1.0;
  std::sort(samples.begin(), samples.end());
  double idx = p * (samples.size() - 1);
  size_t lo = static_cast<size_t>(std::floor(idx));
  size_t hi = static_cast<size_t>(std::ceil(idx));
  if (lo == hi) return samples[lo];
  double frac = idx - lo;
  return samples[lo] * (1.0 - frac) + samples[hi] * frac;
}

int main_init(int argc, char** argv) {
  if (argc < 4) { usage(argv[0]); return 1; }
  uint64_t N = std::stoull(argv[2]);
  uint64_t L = std::stoull(argv[3]);
  int Z = (argc > 4) ? std::stoi(argv[4]) : 4;
  size_t B = (argc > 5) ? static_cast<size_t>(std::stoull(argv[5])) : 4096;
  roram::Params params(N, L, Z, B);
  std::cout << "Params: N=" << params.N << " L=" << params.L << " Z=" << params.Z
            << " B=" << params.B << " ell=" << params.ell << " h=" << params.h << "\n";
  return 0;
}

int main_bench(int argc, char** argv) {
  if (argc < 4) { usage(argv[0]); return 1; }
  uint64_t N = std::stoull(argv[2]);
  uint64_t L = std::stoull(argv[3]);
  int trials = (argc > 4) ? std::stoi(argv[4]) : 5;
  int Z = 4;
  size_t B = 4096;
  roram::Params params(N, L, Z, B);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);
  std::cout << "Benchmark N=" << N << " L=" << L << " trials=" << trials << "\n";
  for (int exp = 0; exp <= params.ell; ++exp) {
    uint64_t r = 1ULL << exp;
    if (r > N) break;
    double total_ms = 0;
    for (int t = 0; t < trials; ++t) {
      uint64_t a = (N > r) ? (t * 17) % (N - r) : 0;
      auto start = std::chrono::high_resolution_clock::now();
      ram.Access(a, r, "read");
      auto end = std::chrono::high_resolution_clock::now();
      total_ms += std::chrono::duration<double, std::milli>(end - start).count();
    }
    std::cout << "  range 2^" << exp << " (" << r << " blocks): "
              << std::fixed << std::setprecision(2) << (total_ms / trials) << " ms avg\n";
  }
  return 0;
}

int main_read(int argc, char** argv) {
  if (argc < 6) { usage(argv[0]); return 1; }
  uint64_t N = std::stoull(argv[2]);
  uint64_t L = std::stoull(argv[3]);
  uint64_t a = std::stoull(argv[4]);
  uint64_t r = std::stoull(argv[5]);
  roram::Params params(N, L, 4, 4096);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);
  auto data = ram.Access(a, r, "read");
  std::cout << "Read " << data.size() << " blocks\n";
  return 0;
}

int main_write(int argc, char** argv) {
  if (argc < 6) { usage(argv[0]); return 1; }
  uint64_t N = std::stoull(argv[2]);
  uint64_t L = std::stoull(argv[3]);
  uint64_t a = std::stoull(argv[4]);
  uint64_t r = std::stoull(argv[5]);
  roram::Params params(N, L, 4, 4096);
  auto crypto = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram(params, std::move(crypto), true);
  std::vector<std::vector<uint8_t>> D(r, std::vector<uint8_t>(params.B, 0));
  ram.Access(a, r, "write", &D);
  std::cout << "Wrote " << r << " blocks\n";
  return 0;
}

static int main_compare(int argc, char** argv) {
  uint64_t N = 65536;
  uint64_t L = 8192;
  int trials = 5;
  uint64_t seek_penalty_us = 0;
  std::string csv_path;
  std::string file_path;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--N" && i + 1 < argc) { N = std::stoull(argv[++i]); continue; }
    if (arg == "--L" && i + 1 < argc) { L = std::stoull(argv[++i]); continue; }
    if (arg == "--trials" && i + 1 < argc) { trials = std::stoi(argv[++i]); continue; }
    if (arg == "--seek-penalty-us" && i + 1 < argc) { seek_penalty_us = std::stoull(argv[++i]); continue; }
    if (arg == "--csv" && i + 1 < argc) { csv_path = argv[++i]; continue; }
    if (arg == "--file" && i + 1 < argc) { file_path = argv[++i]; continue; }
  }
  const int Z = 4;
  const size_t B = 4096;
  roram::Params params_roram(N, L, Z, B);
  roram::Params params_path(N, 1, Z, B);  // Path ORAM = L=1
  const bool use_file = !file_path.empty();
  const bool count_seeks = use_file;  // enable seek counting when using file storage
  auto crypto1 = std::make_unique<roram::NoOpCrypto>();
  auto crypto2 = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram_roram(params_roram, std::move(crypto1), !use_file, use_file ? (file_path + "_roram") : "", count_seeks);
  roram::PathORAM ram_path(params_path, std::move(crypto2), !use_file, use_file ? (file_path + "_path") : "", count_seeks);

  const int max_exp = std::min(params_roram.ell, 14);
  std::cout << "Compare rORAM vs Path ORAM  N=" << N << " L=" << L << " trials=" << trials;
  if (seek_penalty_us) std::cout << " seek_penalty_us=" << seek_penalty_us;
  std::cout << "\n";
  std::cout << std::string(120, '-') << "\n";
  std::cout << std::setw(12) << "range_size" << std::setw(12) << "scheme"
            << std::setw(14) << "mean_ms" << std::setw(12) << "p50_ms" << std::setw(12) << "p95_ms"
            << std::setw(20) << "time_per_block_ms" << std::setw(14) << "logical_B"
            << std::setw(14) << "mean_seeks" << std::setw(12) << "ci_low" << std::setw(12) << "ci_high" << "\n";
  std::cout << std::string(120, '-') << "\n";

  std::ofstream csv;
  if (!csv_path.empty()) {
    csv.open(csv_path);
    if (csv) csv << "scheme,range_exp,range_size,mean_ms,p50_ms,p95_ms,std_ms,time_per_block_ms,logical_bytes,mean_seeks,ci_low,ci_high\n";
  }

  for (int exp = 0; exp <= max_exp; ++exp) {
    uint64_t r_size = 1ULL << exp;
    if (r_size > N) break;
    uint64_t max_start = (N > r_size) ? (N - r_size) : 0;

    std::vector<double> times_roram, times_path;
    std::vector<uint64_t> seeks_roram, seeks_path;
    times_roram.reserve(static_cast<size_t>(trials));
    times_path.reserve(static_cast<size_t>(trials));
    seeks_roram.reserve(static_cast<size_t>(trials));
    seeks_path.reserve(static_cast<size_t>(trials));
    for (int t = 0; t < trials; ++t) {
      uint64_t a = max_start > 0 ? ((t * 17 + exp * 31) % max_start) : 0;
      uint64_t seek_before_r = ram_roram.get_seek_count();
      auto start = std::chrono::high_resolution_clock::now();
      ram_roram.Access(a, r_size, "read");
      auto end = std::chrono::high_resolution_clock::now();
      uint64_t seek_after_r = ram_roram.get_seek_count();
      double elapsed_r = std::chrono::duration<double, std::milli>(end - start).count();
      double reported_r = elapsed_r + (seek_penalty_us > 0 ? (seek_after_r - seek_before_r) * (seek_penalty_us / 1000.0) : 0);
      times_roram.push_back(reported_r);
      seeks_roram.push_back(seek_after_r - seek_before_r);

      uint64_t seek_before_p = ram_path.get_seek_count();
      double elapsed_p = path_oram_range_read_ms(ram_path, a, r_size);
      uint64_t seek_after_p = ram_path.get_seek_count();
      double reported_p = elapsed_p + (seek_penalty_us > 0 ? (seek_after_p - seek_before_p) * (seek_penalty_us / 1000.0) : 0);
      times_path.push_back(reported_p);
      seeks_path.push_back(seek_after_p - seek_before_p);
    }
    double mean_r, std_r, ci_lo_r, ci_hi_r, mean_p, std_p, ci_lo_p, ci_hi_p;
    mean_std_ci(times_roram, mean_r, std_r, ci_lo_r, ci_hi_r);
    mean_std_ci(times_path, mean_p, std_p, ci_lo_p, ci_hi_p);
    const double per_block_r = r_size > 0 ? mean_r / r_size : 0;
    const double per_block_p = r_size > 0 ? mean_p / r_size : 0;
    const double p50_r = percentile(times_roram, 0.50);
    const double p95_r = percentile(times_roram, 0.95);
    const double p50_p = percentile(times_path, 0.50);
    const double p95_p = percentile(times_path, 0.95);
    const uint64_t logical_bytes = r_size * static_cast<uint64_t>(B);
    uint64_t mean_seeks_r = 0, mean_seeks_p = 0;
    for (size_t i = 0; i < seeks_roram.size(); ++i) { mean_seeks_r += seeks_roram[i]; }
    for (size_t i = 0; i < seeks_path.size(); ++i) { mean_seeks_p += seeks_path[i]; }
    mean_seeks_r = (trials > 0) ? (mean_seeks_r + trials / 2) / trials : 0;
    mean_seeks_p = (trials > 0) ? (mean_seeks_p + trials / 2) / trials : 0;
    std::cout << std::setw(12) << r_size << std::setw(12) << "rORAM"
              << std::fixed << std::setprecision(3)
              << std::setw(14) << mean_r << std::setw(12) << p50_r << std::setw(12) << p95_r
              << std::setw(20) << per_block_r << std::setw(14) << logical_bytes
              << std::setw(14) << mean_seeks_r << std::setw(12) << ci_lo_r << std::setw(12) << ci_hi_r << "\n";
    std::cout << std::setw(12) << r_size << std::setw(12) << "PathORAM"
              << std::setw(14) << mean_p << std::setw(12) << p50_p << std::setw(12) << p95_p
              << std::setw(20) << per_block_p << std::setw(14) << logical_bytes
              << std::setw(14) << mean_seeks_p << std::setw(12) << ci_lo_p << std::setw(12) << ci_hi_p << "\n";
    if (csv.is_open()) {
      csv << "rORAM," << exp << "," << r_size << "," << mean_r << "," << p50_r << "," << p95_r << "," << std_r
          << "," << per_block_r << "," << logical_bytes << "," << mean_seeks_r << "," << ci_lo_r << "," << ci_hi_r << "\n";
      csv << "PathORAM," << exp << "," << r_size << "," << mean_p << "," << p50_p << "," << p95_p << "," << std_p
          << "," << per_block_p << "," << logical_bytes << "," << mean_seeks_p << "," << ci_lo_p << "," << ci_hi_p << "\n";
    }
  }
  if (csv.is_open()) { csv.close(); std::cout << "Wrote " << csv_path << "\n"; }
  return 0;
}

static int main_workload(int argc, char** argv) {
  uint64_t N = 65536;
  uint64_t L = 8192;
  uint64_t queries = 1000;
  uint64_t seed = 0x123456789abcdef0ULL;
  uint64_t seek_penalty_us = 0;
  std::string mode = "fileserver";
  std::string csv_path;
  std::string file_path;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--N" && i + 1 < argc) { N = std::stoull(argv[++i]); continue; }
    if (arg == "--L" && i + 1 < argc) { L = std::stoull(argv[++i]); continue; }
    if (arg == "--queries" && i + 1 < argc) { queries = std::stoull(argv[++i]); continue; }
    if (arg == "--seed" && i + 1 < argc) { seed = std::stoull(argv[++i]); continue; }
    if (arg == "--seek-penalty-us" && i + 1 < argc) { seek_penalty_us = std::stoull(argv[++i]); continue; }
    if (arg == "--mode" && i + 1 < argc) { mode = argv[++i]; continue; }
    if (arg == "--csv" && i + 1 < argc) { csv_path = argv[++i]; continue; }
    if (arg == "--file" && i + 1 < argc) { file_path = argv[++i]; continue; }
  }
  if (mode != "sequential" && mode != "fileserver" && mode != "videoserver") {
    throw std::runtime_error("workload: mode must be sequential|fileserver|videoserver");
  }

  const int Z = 4;
  const size_t B = 4096;
  roram::Params params_roram(N, L, Z, B);
  roram::Params params_path(N, 1, Z, B);
  const bool use_file = !file_path.empty();
  const bool count_seeks = use_file;
  auto trace = make_workload_trace(N, L, queries, mode, seed);

  auto crypto1 = std::make_unique<roram::NoOpCrypto>();
  auto crypto2 = std::make_unique<roram::NoOpCrypto>();
  roram::rORAM ram_roram(params_roram, std::move(crypto1), !use_file, use_file ? (file_path + "_roram") : "", count_seeks);
  roram::PathORAM ram_path(params_path, std::move(crypto2), !use_file, use_file ? (file_path + "_path") : "", count_seeks);

  uint64_t logical_bytes = 0;
  for (const auto& q : trace) logical_bytes += q.r * static_cast<uint64_t>(B);

  auto run_roram = [&]() {
    std::vector<double> per_query_ms;
    per_query_ms.reserve(trace.size());
    uint64_t seek_total = 0;
    for (const auto& q : trace) {
      uint64_t seek_before = ram_roram.get_seek_count();
      auto start = std::chrono::high_resolution_clock::now();
      if (q.is_write) {
        std::vector<std::vector<uint8_t>> d(q.r, std::vector<uint8_t>(B, 0));
        ram_roram.Access(q.a, q.r, "write", &d);
      } else {
        ram_roram.Access(q.a, q.r, "read");
      }
      auto end = std::chrono::high_resolution_clock::now();
      uint64_t seek_after = ram_roram.get_seek_count();
      seek_total += (seek_after - seek_before);
      double ms = std::chrono::duration<double, std::milli>(end - start).count();
      ms += (seek_penalty_us > 0 ? (seek_after - seek_before) * (seek_penalty_us / 1000.0) : 0.0);
      per_query_ms.push_back(ms);
    }
    double mean, stddev, ci_lo, ci_hi;
    mean_std_ci(per_query_ms, mean, stddev, ci_lo, ci_hi);
    return std::tuple<double, double, double, double, double, uint64_t>(
        mean, percentile(per_query_ms, 0.50), percentile(per_query_ms, 0.95), ci_lo, ci_hi, seek_total);
  };

  auto run_path = [&]() {
    std::vector<double> per_query_ms;
    per_query_ms.reserve(trace.size());
    uint64_t seek_total = 0;
    for (const auto& q : trace) {
      uint64_t seek_before = ram_path.get_seek_count();
      double ms = 0.0;
      if (q.is_write) {
        std::vector<std::vector<uint8_t>> d(q.r, std::vector<uint8_t>(B, 0));
        ms = path_oram_range_write_ms(ram_path, q.a, q.r, d);
      } else {
        ms = path_oram_range_read_ms(ram_path, q.a, q.r);
      }
      uint64_t seek_after = ram_path.get_seek_count();
      seek_total += (seek_after - seek_before);
      ms += (seek_penalty_us > 0 ? (seek_after - seek_before) * (seek_penalty_us / 1000.0) : 0.0);
      per_query_ms.push_back(ms);
    }
    double mean, stddev, ci_lo, ci_hi;
    mean_std_ci(per_query_ms, mean, stddev, ci_lo, ci_hi);
    return std::tuple<double, double, double, double, double, uint64_t>(
        mean, percentile(per_query_ms, 0.50), percentile(per_query_ms, 0.95), ci_lo, ci_hi, seek_total);
  };

  auto [mean_r, p50_r, p95_r, ci_lo_r, ci_hi_r, seeks_r] = run_roram();
  auto [mean_p, p50_p, p95_p, ci_lo_p, ci_hi_p, seeks_p] = run_path();

  auto qps = [](double mean_ms) { return mean_ms > 0 ? (1000.0 / mean_ms) : 0.0; };
  auto mbps = [logical_bytes](double mean_ms) {
    return mean_ms > 0 ? ((logical_bytes / 1048576.0) / (mean_ms / 1000.0)) : 0.0;
  };

  std::cout << "Workload throughput benchmark  mode=" << mode << " queries=" << queries
            << " N=" << N << " L=" << L;
  if (seek_penalty_us) std::cout << " seek_penalty_us=" << seek_penalty_us;
  std::cout << "\n";
  std::cout << std::string(132, '-') << "\n";
  std::cout << std::setw(12) << "scheme" << std::setw(12) << "mean_ms" << std::setw(12) << "p50_ms"
            << std::setw(12) << "p95_ms" << std::setw(14) << "qps" << std::setw(14) << "mbps"
            << std::setw(14) << "mean_seeks" << std::setw(12) << "ci_low" << std::setw(12) << "ci_high" << "\n";
  std::cout << std::string(132, '-') << "\n";
  std::cout << std::fixed << std::setprecision(3)
            << std::setw(12) << "rORAM" << std::setw(12) << mean_r << std::setw(12) << p50_r
            << std::setw(12) << p95_r << std::setw(14) << qps(mean_r) << std::setw(14) << mbps(mean_r)
            << std::setw(14) << (queries > 0 ? (seeks_r / queries) : 0) << std::setw(12) << ci_lo_r << std::setw(12) << ci_hi_r << "\n";
  std::cout << std::setw(12) << "PathORAM" << std::setw(12) << mean_p << std::setw(12) << p50_p
            << std::setw(12) << p95_p << std::setw(14) << qps(mean_p) << std::setw(14) << mbps(mean_p)
            << std::setw(14) << (queries > 0 ? (seeks_p / queries) : 0) << std::setw(12) << ci_lo_p << std::setw(12) << ci_hi_p << "\n";

  if (!csv_path.empty()) {
    std::ofstream csv(csv_path);
    if (csv) {
      csv << "scheme,mode,queries,N,L,mean_ms,p50_ms,p95_ms,queries_per_sec,mb_per_sec,mean_seeks,ci_low,ci_high\n";
      csv << "rORAM," << mode << "," << queries << "," << N << "," << L << "," << mean_r << "," << p50_r << "," << p95_r
          << "," << qps(mean_r) << "," << mbps(mean_r) << "," << (queries > 0 ? (seeks_r / queries) : 0)
          << "," << ci_lo_r << "," << ci_hi_r << "\n";
      csv << "PathORAM," << mode << "," << queries << "," << N << "," << L << "," << mean_p << "," << p50_p << "," << p95_p
          << "," << qps(mean_p) << "," << mbps(mean_p) << "," << (queries > 0 ? (seeks_p / queries) : 0)
          << "," << ci_lo_p << "," << ci_hi_p << "\n";
      std::cout << "Wrote " << csv_path << "\n";
    }
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { usage(argv[0]); return 1; }
  std::string cmd = argv[1];
  if (cmd == "init") return main_init(argc, argv);
  if (cmd == "bench") return main_bench(argc, argv);
  if (cmd == "read") return main_read(argc, argv);
  if (cmd == "write") return main_write(argc, argv);
  if (cmd == "compare") return main_compare(argc, argv);
  if (cmd == "workload") return main_workload(argc, argv);
  usage(argv[0]);
  return 1;
}
