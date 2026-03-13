#!/usr/bin/env bash
set -euo pipefail

BIN="./roram_main"
OUTDIR="/tmp/ndss_largescale"
BACKING_PREFIX="/tmp/ndss_largescale/device"
SEEK_PENALTY_US=50
PATH_RECURSIVE_PM=0
PATH_PM_ACCESSES=0

usage() {
  cat <<EOF
Run larger-scale SSD-focused NDSS-style experiments with disk-safe cleanup between runs.

This executes:
  1) compare N=16384 L=1024
  2) compare N=32768 L=2048
  3) workload modes at N=16384 L=1024

Usage:
  $0 [--bin path] [--outdir dir] [--backing-prefix path] [--seek-penalty-us us]
     [--path-recursive-pm] [--path-pm-accesses k]
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN="$2"; shift 2 ;;
    --outdir) OUTDIR="$2"; shift 2 ;;
    --backing-prefix) BACKING_PREFIX="$2"; shift 2 ;;
    --seek-penalty-us) SEEK_PENALTY_US="$2"; shift 2 ;;
    --path-recursive-pm) PATH_RECURSIVE_PM=1; shift ;;
    --path-pm-accesses) PATH_PM_ACCESSES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

mkdir -p "$OUTDIR"
ts="$(date +%Y%m%d_%H%M%S)"

run_compare() {
  local n="$1" l="$2" t="$3"
  local name="compare_n${n}_l${l}_t${t}_sp${SEEK_PENALTY_US}_${ts}"
  "$BIN" compare --N "$n" --L "$l" --trials "$t" \
    --seek-penalty-us "$SEEK_PENALTY_US" \
    --file "$BACKING_PREFIX" \
    --csv "${OUTDIR}/${name}.csv" \
    $( [[ "$PATH_RECURSIVE_PM" -eq 1 ]] && printf -- "--path-recursive-pm" ) \
    $( [[ "$PATH_PM_ACCESSES" -gt 0 ]] && printf -- "--path-pm-accesses %s" "$PATH_PM_ACCESSES" ) | tee "${OUTDIR}/${name}.txt"
  rm -f "${BACKING_PREFIX}"*
}

run_workload_mode() {
  local mode="$1" q="$2" n="$3" l="$4"
  local name="workload_${mode}_q${q}_n${n}_l${l}_sp${SEEK_PENALTY_US}_${ts}"
  "$BIN" workload --mode "$mode" --queries "$q" --N "$n" --L "$l" \
    --seek-penalty-us "$SEEK_PENALTY_US" \
    --file "$BACKING_PREFIX" \
    --csv "${OUTDIR}/${name}.csv" \
    $( [[ "$PATH_RECURSIVE_PM" -eq 1 ]] && printf -- "--path-recursive-pm" ) \
    $( [[ "$PATH_PM_ACCESSES" -gt 0 ]] && printf -- "--path-pm-accesses %s" "$PATH_PM_ACCESSES" ) | tee "${OUTDIR}/${name}.txt"
  rm -f "${BACKING_PREFIX}"*
}

echo "[1/3] compare at N=16384,L=1024"
run_compare 16384 1024 5

echo "[2/3] compare at N=32768,L=2048"
run_compare 32768 2048 3

echo "[3/3] workload modes at N=16384,L=1024"
run_workload_mode sequential 300 16384 1024
run_workload_mode fileserver 300 16384 1024
run_workload_mode videoserver 200 16384 1024

echo "Wrote outputs to ${OUTDIR}"
