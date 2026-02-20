#include "roram/roram.hpp"
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

static void usage(const char* prog) {
  std::cerr << "Usage: " << prog << " <init|read|write|bench|compare> [options]\n"
            << "  init N L [Z] [B]     - init params (N blocks, L max range, Z bucket size, B block bytes)\n"
            << "  read N L a r         - read range [a, a+r) (params N, L)\n"
            << "  write N L a r        - write range [a, a+r) with zeros (params N, L)\n"
            << "  bench N L [trials]   - benchmark range sizes (default 5 trials)\n"
            << "  compare [--N N] [--L L] [--trials T] [--csv path] [--file path] [--seek-penalty-us N]\n"
            << "          - rORAM vs Path ORAM; use --seek-penalty-us to simulate seek cost (crossover)\n";
}

// Path ORAM: range read as r sequential Access(addr, 1, "read"). Returns total time in ms.
static double path_oram_range_read_ms(roram::rORAM& ram, uint64_t a, uint64_t r) {
  auto start = std::chrono::high_resolution_clock::now();
  for (uint64_t i = 0; i < r; ++i)
    ram.Access(a + i, 1, "read");
  auto end = std::chrono::high_resolution_clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
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
  roram::rORAM ram_path(params_path, std::move(crypto2), !use_file, use_file ? (file_path + "_path") : "", count_seeks);

  const int max_exp = std::min(params_roram.ell, 14);
  std::cout << "Compare rORAM vs Path ORAM  N=" << N << " L=" << L << " trials=" << trials;
  if (seek_penalty_us) std::cout << " seek_penalty_us=" << seek_penalty_us;
  std::cout << "\n";
  std::cout << std::string(120, '-') << "\n";
  std::cout << std::setw(12) << "range_size" << std::setw(12) << "scheme"
            << std::setw(16) << "total_ms" << std::setw(20) << "time_per_block_ms"
            << std::setw(14) << "mean_seeks" << std::setw(12) << "ci_low" << std::setw(12) << "ci_high" << "\n";
  std::cout << std::string(120, '-') << "\n";

  std::ofstream csv;
  if (!csv_path.empty()) {
    csv.open(csv_path);
    if (csv) csv << "scheme,range_exp,range_size,mean_ms,std_ms,time_per_block_ms,mean_seeks,ci_low,ci_high\n";
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
    uint64_t mean_seeks_r = 0, mean_seeks_p = 0;
    for (size_t i = 0; i < seeks_roram.size(); ++i) { mean_seeks_r += seeks_roram[i]; }
    for (size_t i = 0; i < seeks_path.size(); ++i) { mean_seeks_p += seeks_path[i]; }
    mean_seeks_r = (trials > 0) ? (mean_seeks_r + trials / 2) / trials : 0;
    mean_seeks_p = (trials > 0) ? (mean_seeks_p + trials / 2) / trials : 0;
    std::cout << std::setw(12) << r_size << std::setw(12) << "rORAM"
              << std::fixed << std::setprecision(3)
              << std::setw(16) << mean_r << std::setw(20) << per_block_r
              << std::setw(14) << mean_seeks_r << std::setw(12) << ci_lo_r << std::setw(12) << ci_hi_r << "\n";
    std::cout << std::setw(12) << r_size << std::setw(12) << "PathORAM"
              << std::setw(16) << mean_p << std::setw(20) << per_block_p
              << std::setw(14) << mean_seeks_p << std::setw(12) << ci_lo_p << std::setw(12) << ci_hi_p << "\n";
    if (csv.is_open()) {
      csv << "rORAM," << exp << "," << r_size << "," << mean_r << "," << std_r << "," << per_block_r << "," << mean_seeks_r << "," << ci_lo_r << "," << ci_hi_r << "\n";
      csv << "PathORAM," << exp << "," << r_size << "," << mean_p << "," << std_p << "," << per_block_p << "," << mean_seeks_p << "," << ci_lo_p << "," << ci_hi_p << "\n";
    }
  }
  if (csv.is_open()) { csv.close(); std::cout << "Wrote " << csv_path << "\n"; }
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
  usage(argv[0]);
  return 1;
}
