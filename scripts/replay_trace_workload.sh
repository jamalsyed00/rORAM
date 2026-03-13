#!/usr/bin/env bash
set -euo pipefail

BIN="./roram_main"
TRACE=""
N=65536
L=8192
SEEK_PENALTY_US=50
CSV=""
BACKING_PREFIX=""

usage() {
  cat <<EOF
Replay a real trace CSV through the workload benchmark.

Expected trace CSV format:
  op,a,r
  read,123,8
  write,456,4

Usage:
  $0 --trace path [--N N] [--L L] [--seek-penalty-us us] [--csv out.csv] [--backing-prefix path]
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --trace) TRACE="$2"; shift 2 ;;
    --N) N="$2"; shift 2 ;;
    --L) L="$2"; shift 2 ;;
    --seek-penalty-us) SEEK_PENALTY_US="$2"; shift 2 ;;
    --csv) CSV="$2"; shift 2 ;;
    --backing-prefix) BACKING_PREFIX="$2"; shift 2 ;;
    --bin) BIN="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$TRACE" ]]; then
  echo "--trace is required" >&2
  usage
  exit 1
fi
if [[ -z "$CSV" ]]; then
  CSV="/tmp/trace_replay_$(date +%Y%m%d_%H%M%S).csv"
fi

cmd=( "$BIN" workload --N "$N" --L "$L" --trace "$TRACE" --seek-penalty-us "$SEEK_PENALTY_US" --csv "$CSV" )
if [[ -n "$BACKING_PREFIX" ]]; then
  cmd+=( --file "$BACKING_PREFIX" )
fi

"${cmd[@]}"
echo "Wrote ${CSV}"
