#!/usr/bin/env bash
set -euo pipefail

BIN="./roram_main"
N=4096
L=256
TRIALS=5
SEEK_PENALTY_US=0
OUTDIR="bench_out_ndss"
BACKING_PREFIX="/tmp/roram_ndss_ssd/device"
CLEANUP_BACKING=1
PATH_RECURSIVE_PM=0
PATH_PM_ACCESSES=0

usage() {
  cat <<EOF
Replicate NDSS'19 rORAM query-access-time and throughput-style measurements on SSD-backed file storage.

This script runs rORAM vs PathORAM compare in file-backed mode and outputs:
  1) access_time CSV (direct benchmark output)
  2) throughput CSV derived from query latency:
       queries_per_sec = 1000 / mean_ms
       mb_per_sec      = (logical_bytes / 2^20) / (mean_ms / 1000)

Defaults are scaled down from the paper's full 16GB setup to fit local disk.

Usage:
  $0 [options]

Options:
  --bin <path>              Path to roram_main (default: ${BIN})
  --N <num>                 Number of blocks (default: ${N})
  --L <num>                 Max range size (default: ${L})
  --trials <num>            Trials per range size (default: ${TRIALS})
  --seek-penalty-us <num>   Additional per-seek latency model (default: ${SEEK_PENALTY_US})
  --outdir <dir>            Output directory (default: ${OUTDIR})
  --backing-prefix <path>   File prefix for ORAM trees on SSD (default: ${BACKING_PREFIX})
  --path-recursive-pm       Enable recursive-PM-style Path ORAM baseline emulation
  --path-pm-accesses <num>  Override PM accesses per Path data access (default: auto)
  --no-cleanup              Keep backing tree files after run
  -h|--help                 Show this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN="$2"; shift 2 ;;
    --N) N="$2"; shift 2 ;;
    --L) L="$2"; shift 2 ;;
    --trials) TRIALS="$2"; shift 2 ;;
    --seek-penalty-us) SEEK_PENALTY_US="$2"; shift 2 ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    --backing-prefix) BACKING_PREFIX="$2"; shift 2 ;;
    --path-recursive-pm) PATH_RECURSIVE_PM=1; shift ;;
    --path-pm-accesses) PATH_PM_ACCESSES="$2"; shift 2 ;;
    --no-cleanup) CLEANUP_BACKING=0; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ ! -x "$BIN" ]]; then
  echo "Binary not executable: $BIN" >&2
  exit 1
fi

mkdir -p "$OUTDIR"
ts="$(date +%Y%m%d_%H%M%S)"
access_csv="${OUTDIR}/ndss_access_time_${ts}.csv"
throughput_csv="${OUTDIR}/ndss_throughput_${ts}.csv"
log_txt="${OUTDIR}/ndss_run_${ts}.txt"

echo "[1/3] Running file-backed query access-time benchmark..."
"$BIN" compare \
  --N "$N" \
  --L "$L" \
  --trials "$TRIALS" \
  --seek-penalty-us "$SEEK_PENALTY_US" \
  --file "$BACKING_PREFIX" \
  --csv "$access_csv" \
  $( [[ "$PATH_RECURSIVE_PM" -eq 1 ]] && printf -- "--path-recursive-pm" ) \
  $( [[ "$PATH_PM_ACCESSES" -gt 0 ]] && printf -- "--path-pm-accesses %s" "$PATH_PM_ACCESSES" ) | tee "$log_txt"

echo "[2/3] Deriving throughput table from mean query latency..."
awk -F, 'BEGIN{
  OFS=",";
  print "scheme,range_exp,range_size,queries_per_sec,mb_per_sec,mean_ms,p50_ms,p95_ms,logical_bytes,mean_seeks";
}
NR>1{
  mean_ms=$4+0.0;
  logical_bytes=$9+0.0;
  qps=(mean_ms>0)?(1000.0/mean_ms):0.0;
  mbps=(mean_ms>0)?((logical_bytes/1048576.0)/(mean_ms/1000.0)):0.0;
  printf "%s,%s,%s,%.6f,%.6f,%s,%s,%s,%s,%s\n",
    $1,$2,$3,qps,mbps,$4,$5,$6,$9,$10;
}' "$access_csv" > "$throughput_csv"

echo "[3/3] Summaries"
echo "Access-time CSV:    $access_csv"
echo "Throughput CSV:     $throughput_csv"
echo "Console log:        $log_txt"

if [[ "$CLEANUP_BACKING" -eq 1 ]]; then
  rm -f "${BACKING_PREFIX}"*
  echo "Removed backing files: ${BACKING_PREFIX}*"
else
  echo "Kept backing files: ${BACKING_PREFIX}*"
fi
