#!/usr/bin/env bash
set -euo pipefail

N=65536
L=8192
TRIALS=5
SEEK_PENALTY_US=0
OUTDIR="bench_out"
SSD_PATH=""
HDD_PATH=""
BIN="./roram_main"

usage() {
  cat <<EOF
Usage: $0 [options]

Options:
  --N <num>                 Number of logical blocks (default: ${N})
  --L <num>                 Max range size (default: ${L})
  --trials <num>            Trials per range (default: ${TRIALS})
  --seek-penalty-us <num>   Simulated seek penalty in microseconds (default: ${SEEK_PENALTY_US})
  --outdir <dir>            Output directory for CSVs (default: ${OUTDIR})
  --ssd-path <prefix>       File prefix for SSD-backed run (required for SSD run)
  --hdd-path <prefix>       File prefix for HDD-backed run (required for HDD run)
  --bin <path>              Path to roram_main binary (default: ${BIN})
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --N) N="$2"; shift 2 ;;
    --L) L="$2"; shift 2 ;;
    --trials) TRIALS="$2"; shift 2 ;;
    --seek-penalty-us) SEEK_PENALTY_US="$2"; shift 2 ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    --ssd-path) SSD_PATH="$2"; shift 2 ;;
    --hdd-path) HDD_PATH="$2"; shift 2 ;;
    --bin) BIN="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ ! -x "$BIN" ]]; then
  echo "Binary not executable: $BIN" >&2
  exit 1
fi

mkdir -p "$OUTDIR"
TS="$(date +%Y%m%d_%H%M%S)"

run_case() {
  local label="$1"
  local csv="$2"
  shift 2
  echo "[run] ${label} -> ${csv}"
  "$BIN" compare --N "$N" --L "$L" --trials "$TRIALS" --seek-penalty-us "$SEEK_PENALTY_US" --csv "$csv" "$@"
}

RAM_CSV="${OUTDIR}/compare_ram_${TS}.csv"
run_case "RAM (in-memory)" "$RAM_CSV"

if [[ -n "$SSD_PATH" ]]; then
  SSD_CSV="${OUTDIR}/compare_ssd_${TS}.csv"
  run_case "SSD (file-backed)" "$SSD_CSV" --file "${SSD_PATH}_run_${TS}"
fi

if [[ -n "$HDD_PATH" ]]; then
  HDD_CSV="${OUTDIR}/compare_hdd_${TS}.csv"
  run_case "HDD (file-backed)" "$HDD_CSV" --file "${HDD_PATH}_run_${TS}"
fi

echo
echo "Generated CSVs:"
ls -1 "${OUTDIR}"/*"${TS}".csv
