#!/usr/bin/env bash
# =============================================================================
# run_extended_benchmarks.sh -- compile the extended structural benchmarks and
# print their resource estimates (paper Table tab:extbench).
#
# Each benchmark is a hand-written MLIR circuit in test/Integration/ that is run
# through its pipeline plus --ftqc-resource-estimate; the estimate is emitted in
# the ftqc.resource_annotation attribute (physical qubits, T-count, CNOTs, EC
# rounds, logical error rate).
#
# Usage (from anywhere):
#   bash utils/run_extended_benchmarks.sh
#
# Env overrides:
#   FTQC_OPT   path to ftqc-opt (default: build/src/ftqc-opt, then install/bin)
#   LLVM       LLVM/MLIR install prefix (default below); $LLVM/lib is added to
#              LD_LIBRARY_PATH so ftqc-opt can find the shared MLIR libraries
# =============================================================================
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --- locate ftqc-opt ---------------------------------------------------------
FTQC_OPT="${FTQC_OPT:-}"
if [[ -z "$FTQC_OPT" ]]; then
  for c in "$ROOT/build/src/ftqc-opt" "$ROOT/install/bin/ftqc-opt" "$(command -v ftqc-opt 2>/dev/null)"; do
    [[ -x "$c" ]] && FTQC_OPT="$c" && break
  done
fi
if [[ -z "$FTQC_OPT" || ! -x "$FTQC_OPT" ]]; then
  echo "ERROR: ftqc-opt not found. Build it (./build.sh) or set FTQC_OPT." >&2
  exit 1
fi

# --- shared libraries (libftqc + LLVM/MLIR) ---------------------------------
LLVM="${LLVM:-${ROOT}/../llvm-project/install}"   # same default as build.sh
export LD_LIBRARY_PATH="$ROOT/install/lib:$LLVM/lib:${LD_LIBRARY_PATH:-}"

echo "ftqc-opt: $FTQC_OPT"
echo "=========================================================================="
printf "%-22s | %s\n" "Benchmark" "resource_annotation (physical qubits / T / CNOT / EC / p_L)"
echo "--------------------------------------------------------------------------"

# benchmark | pipeline passes (space-separated)
run_one() {
  local name="$1" file="$2"; shift 2
  local out
  out="$("$FTQC_OPT" "$ROOT/test/Integration/$file" "$@" --ftqc-resource-estimate 2>&1)"
  # print each resource_annotation found (multi-code-memory yields several)
  local anns; anns="$(printf '%s\n' "$out" | grep -o 'ftqc.resource_annotation {[^}]*}')"
  if [[ -z "$anns" ]]; then
    printf "%-22s | (no annotation; see: %s)\n" "$name" "$file"
  else
    local first=1
    while IFS= read -r a; do
      [[ -z "$a" ]] && continue
      if [[ $first == 1 ]]; then printf "%-22s | %s\n" "$name" "$a"; first=0
      else printf "%-22s | %s\n" "" "$a"; fi
    done <<< "$anns"
  fi
}

run_one "Multi-code memory"    multi-code-memory.mlir -split-input-file
run_one "Lattice-surgery CNOT" lattice-surgery-cnot.mlir
run_one "Distillation factory" distillation-factory.mlir
run_one "QAOA layer (fixed)"   qaoa-layer.mlir     --ftqc-insert-syndrome --ftqc-transversal-gates
run_one "VQE ansatz (HWE-4)"   vqe-ansatz.mlir     --ftqc-insert-syndrome --ftqc-transversal-gates
run_one "Grover-4 (MCX)"       grover-mcx.mlir     --ftqc-decompose-mcx
run_one "QAOA-4 (arb. angle)"  qaoa-arbitrary.mlir --ftqc-synthesize-rz --ftqc-insert-syndrome --ftqc-transversal-gates

echo "=========================================================================="
echo "Done.  (These map to the rows of Table tab:extbench.)"
