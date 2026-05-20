#!/usr/bin/env bash
# run_iqm_json.sh — Generate IQM JSON for all 4 FTQC benchmark circuits.
#
# Usage:
#   ./run_iqm_json.sh [OPTIONS]
#
#   --insert-syndrome    Prepend --ftqc-insert-syndrome before lowering.
#   --expand-syndromes   Expand syndrome ops into physical CZ ancilla circuits
#                        (only meaningful when --insert-syndrome is also given).
#   --pre-pass PASS      Prepend an arbitrary ftqc-opt pass flag before lowering.
#                        Can be repeated; passes are applied in order given.
#                        Example: --pre-pass --ftqc-insert-syndrome \
#                                 --pre-pass --ftqc-transversal-gates
#   --outdir DIR         Write JSON files to DIR (default: iqm_json_out/).
#
# Output files:
#   bell-state.json   bell-state-ft.json   ghz4.json   grover.json   qft3.json
#
# Requires: build/src/ftqc-opt (run `cd build && ninja ftqc-opt` first).

set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
FTQC_OPT="$REPO/build/src/ftqc-opt"
OUTDIR="$REPO/iqm_json_out"
EXPAND_SYNDROMES=false
INSERT_SYNDROME=false
PRE_PASSES=()

# Parse arguments.
while [[ $# -gt 0 ]]; do
  case "$1" in
    --insert-syndrome)  INSERT_SYNDROME=true; shift ;;
    --expand-syndromes) EXPAND_SYNDROMES=true; shift ;;
    --pre-pass)         PRE_PASSES+=("$2"); shift 2 ;;
    --outdir)           OUTDIR="$2"; shift 2 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
done

# --insert-syndrome is shorthand for --pre-pass --ftqc-insert-syndrome.
if $INSERT_SYNDROME; then
  PRE_PASSES=(--ftqc-insert-syndrome "${PRE_PASSES[@]}")
fi

if [[ ! -x "$FTQC_OPT" ]]; then
  echo "ERROR: $FTQC_OPT not found. Run: cd $REPO/build && ninja ftqc-opt" >&2
  exit 1
fi

mkdir -p "$OUTDIR"

# Build pass options string.
if $EXPAND_SYNDROMES; then
  PASS_OPTS="include-comments=false expand-syndromes=true"
else
  PASS_OPTS="include-comments=false expand-syndromes=false"
fi
PASS="--ftqc-lower-to-iqm-json=$PASS_OPTS"

# ---------------------------------------------------------------------------
# 4 benchmark circuits
# ---------------------------------------------------------------------------
declare -A CIRCUITS=(
  ["bell-state"]="test/Transforms/QASM3/bell-state.mlir"
  ["bell-state-ft"]="test/Integration/bell-state-ft.mlir"
  ["ghz4"]="test/Integration/ghz4-steane.mlir"
  ["grover"]="test/Integration/grover-ft.mlir"
  ["qft3"]="test/Integration/qft3-steane.mlir"
)

# Circuits that need syndrome insertion to be well-formed for lowering.
# (grover and qft3 use --ftqc-insert-syndrome first in their lit tests)
declare -A EXTRA_PASSES=(
  ["bell-state"]=""
  ["bell-state-ft"]=""
  ["ghz4"]=""
  ["grover"]=""
  ["qft3"]=""
)

PAD=16
PASS_COUNT=0
FAIL_COUNT=0

echo ""
echo "ftqc IQM JSON emitter — benchmark circuits"
echo "  ftqc-opt  : $FTQC_OPT"
echo "  output    : $OUTDIR/"
echo "  pre-passes: ${PRE_PASSES[*]:-(none)}"
echo "  expand-syndromes: $EXPAND_SYNDROMES"
echo ""
printf "  %-${PAD}s  %-10s  %-8s  %-8s\n" "circuit" "status" "#inst" "#qubits"
printf "  %-${PAD}s  %-10s  %-8s  %-8s\n" "---------------" "----------" "--------" "--------"

for NAME in bell-state bell-state-ft ghz4 grover qft3; do
  SRC="$REPO/${CIRCUITS[$NAME]}"
  OUTFILE="$OUTDIR/${NAME}.json"
  EXTRA="${EXTRA_PASSES[$NAME]}"

  if [[ ! -f "$SRC" ]]; then
    printf "  %-${PAD}s  %-10s\n" "$NAME" "SKIP (no src)"
    continue
  fi

  # Build command: pre-passes (global) + per-circuit extra + IQM JSON pass.
  CMD=("$FTQC_OPT" "${PRE_PASSES[@]}")
  [[ -n "$EXTRA" ]] && CMD+=($EXTRA)
  CMD+=("$PASS" "$SRC")

  # Run — stderr holds the JSON, stdout holds the MLIR (suppress it).
  if "${CMD[@]}" 2>"$OUTFILE" 1>/dev/null; then
    # Parse instruction count and unique qubit names from JSON.
    INST_COUNT=$(grep -o '"name"' "$OUTFILE" | wc -l)
    # Subtract 1 for the top-level "name" field of the circuit object.
    INST_COUNT=$(( INST_COUNT - 1 ))
    QUBIT_COUNT=$(grep -o '"QB[A-Z0-9]*"' "$OUTFILE" | sort -u | wc -l)
    printf "  %-${PAD}s  %-10s  %-8s  %-8s\n" \
      "$NAME" "OK" "$INST_COUNT" "$QUBIT_COUNT"
    PASS_COUNT=$(( PASS_COUNT + 1 ))
  else
    printf "  %-${PAD}s  %-10s\n" "$NAME" "FAIL"
    FAIL_COUNT=$(( FAIL_COUNT + 1 ))
  fi
done

echo ""
echo "  Results: $PASS_COUNT passed, $FAIL_COUNT failed"
echo ""

if (( FAIL_COUNT > 0 )); then
  exit 1
fi

echo "JSON files written to $OUTDIR/"
echo ""
echo "To submit a circuit to IQM Resonance, set:"
echo "  export IQM_BASE_URL=https://resonance.iqm.tech"
echo "  export RESONANCE_API_KEY=<your-token>"
echo "  export IQM_QC_ALIAS=<device>   # e.g. emerald:mock (emulator)"
echo ""
