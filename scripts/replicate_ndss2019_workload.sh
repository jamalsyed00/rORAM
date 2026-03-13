#!/usr/bin/env bash
set -euo pipefail

BIN="./roram_main"
N=4096
L=256
QUERIES=400
SEEK_PENALTY_US=0
OUTDIR="/tmp/ndss_workload"
BACKING_PREFIX="/tmp/ndss_workload/device"
CLEANUP_BACKING=1
PATH_RECURSIVE_PM=0
PATH_PM_ACCESSES=0

usage() {
  cat <<EOF
Replicate NDSS'19 throughput-style workload experiments with synthetic traces.

Runs 3 workload modes using the same synchronous issue model:
  - sequential
  - fileserver
  - videoserver

Usage:
  $0 [options]

Options:
  --bin <path>              roram_main path (default: ${BIN})
  --N <num>                 Number of blocks (default: ${N})
  --L <num>                 Max range size (default: ${L})
  --queries <num>           Number of queries per workload (default: ${QUERIES})
  --seek-penalty-us <num>   Per-seek delay model (default: ${SEEK_PENALTY_US})
  --outdir <dir>            Output directory (default: ${OUTDIR})
  --backing-prefix <path>   File-backed ORAM prefix (default: ${BACKING_PREFIX})
  --path-recursive-pm       Enable recursive-PM-style Path ORAM baseline emulation
  --path-pm-accesses <num>  Override PM accesses per Path data access (default: auto)
  --no-cleanup              Keep backing files after each run
  -h|--help                 Show this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN="$2"; shift 2 ;;
    --N) N="$2"; shift 2 ;;
    --L) L="$2"; shift 2 ;;
    --queries) QUERIES="$2"; shift 2 ;;
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
summary="${OUTDIR}/ndss_workload_summary_${ts}.csv"
echo "scheme,mode,queries,N,L,mean_ms,p50_ms,p95_ms,queries_per_sec,mb_per_sec,mean_seeks,ci_low,ci_high" > "$summary"

for mode in sequential fileserver videoserver; do
  out_csv="${OUTDIR}/ndss_workload_${mode}_${ts}.csv"
  out_txt="${OUTDIR}/ndss_workload_${mode}_${ts}.txt"
  echo "[run] mode=${mode}"
  "$BIN" workload \
    --mode "$mode" \
    --queries "$QUERIES" \
    --N "$N" \
    --L "$L" \
    --seek-penalty-us "$SEEK_PENALTY_US" \
    --file "$BACKING_PREFIX" \
    --csv "$out_csv" \
    $( [[ "$PATH_RECURSIVE_PM" -eq 1 ]] && printf -- "--path-recursive-pm" ) \
    $( [[ "$PATH_PM_ACCESSES" -gt 0 ]] && printf -- "--path-pm-accesses %s" "$PATH_PM_ACCESSES" ) | tee "$out_txt"
  tail -n +2 "$out_csv" >> "$summary"
  if [[ "$CLEANUP_BACKING" -eq 1 ]]; then
    rm -f "${BACKING_PREFIX}"*
  fi
done

echo "Wrote ${summary}"
