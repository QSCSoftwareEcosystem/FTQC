#!/usr/bin/env bash
# run_iqs_validate.sh — end-to-end validation of --ftqc-lower-to-iqs-cpp
#
# Pipeline:
#   FTQC MLIR  ──► ftqc-opt --ftqc-lower-to-iqs-cpp  ──► <circuit>.cpp
#   <circuit>.cpp  ──► g++ + libiqs  ──► <circuit>_sim
#   <circuit>_sim [shots]  ──► stdout results  ──► checked & summarised
#
# Circuits tested:
#   1. Bell state        (ideal)
#   2. GHZ-3             (ideal)
#   3. GHZ-4             (ideal)
#   4. Grover 2-qubit    (ideal, target |11>)
#   5. Bell              (noisy  T1=5,   T2=2,   moderate)
#   6. Bell              (noisy  T1=0.5, T2=0.2, aggressive)
#
# Usage:
#   ./run_iqs_validate.sh            # use defaults
#   IQS_ROOT=/path/to/intel-qs ./run_iqs_validate.sh
#   SHOTS=200 ./run_iqs_validate.sh

set -euo pipefail

# ── Configurable paths ────────────────────────────────────────────────────────
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
IQS_ROOT="${IQS_ROOT:-${REPO_ROOT}/../intel-qs}"
MPI_INC="${MPI_INC:-/usr/lib/x86_64-linux-gnu/openmpi/include}"
MPI_LIB="${MPI_LIB:-/usr/lib/x86_64-linux-gnu/openmpi/lib}"
FTQC_OPT="${REPO_ROOT}/install/bin/ftqc-opt"
WORKDIR="${WORKDIR:-/tmp/ftqc_iqs_validate}"
SHOTS="${SHOTS:-40}"

# ── Colours ───────────────────────────────────────────────────────────────────
G="\033[0;32m"; Y="\033[0;33m"; R="\033[0;31m"; B="\033[0;34m"; N="\033[0m"

banner() { echo -e "\n${B}══════════════════════════════════════════════════${N}"; \
           echo -e "${B}  $1${N}"; \
           echo -e "${B}══════════════════════════════════════════════════${N}"; }
ok()     { echo -e "  ${G}✓${N} $*"; }
warn()   { echo -e "  ${Y}⚠${N} $*"; }
fail()   { echo -e "  ${R}✗${N} $*"; exit 1; }

# ── Preflight ─────────────────────────────────────────────────────────────────
banner "Preflight checks"

[ -x "$FTQC_OPT" ]                         || fail "ftqc-opt not found at $FTQC_OPT"
[ -f "${IQS_ROOT}/include/qureg.hpp" ]     || fail "Intel-QS headers not found at ${IQS_ROOT}/include"
[ -f "${IQS_ROOT}/build/lib/libiqs.so" ]   || fail "libiqs.so not found at ${IQS_ROOT}/build/lib"
[ -f "${MPI_INC}/mpi.h" ]                  || fail "MPI headers not found at ${MPI_INC}"
command -v g++ >/dev/null                  || fail "g++ not found"

ok "ftqc-opt : $FTQC_OPT"
ok "Intel-QS : $IQS_ROOT"
ok "MPI      : $MPI_INC"
ok "Work dir : $WORKDIR"
ok "Shots    : $SHOTS"

mkdir -p "$WORKDIR"

# ── Compiler flags ────────────────────────────────────────────────────────────
CXXFLAGS="-std=c++17 -O2 -I${IQS_ROOT}/include -I${MPI_INC}"
LDFLAGS="-L${IQS_ROOT}/build/lib -liqs -L${MPI_LIB} -lmpi -lm \
         -Wl,-rpath,${IQS_ROOT}/build/lib -Wl,-rpath,${MPI_LIB}"

# ── Helper: emit C++, compile, return binary path ─────────────────────────────
# Usage: build_circuit <name> <mlir_file> [extra_pass_options]
build_circuit() {
    local name="$1" mlir="$2" opts="${3:-}"
    local cpp="${WORKDIR}/${name}.cpp"
    local bin="${WORKDIR}/${name}_sim"

    echo -n "  Generating ${name}.cpp ... " >&2
    "$FTQC_OPT" "--ftqc-lower-to-iqs-cpp=output-file=${cpp} ${opts}" "$mlir" >/dev/null 2>&1
    [ -f "$cpp" ] || { echo "FAIL" >&2; fail "emission failed for $name"; }
    echo "done ($(wc -l < "$cpp") lines)" >&2

    echo -n "  Compiling  ${name}_sim  ... " >&2
    # shellcheck disable=SC2086
    g++ $CXXFLAGS "$cpp" $LDFLAGS -o "$bin" 2>&1 >&2 \
        || { fail "compilation failed for $name"; }
    echo "done" >&2
    echo "$bin"
}

# ── Helper: run sim, capture results, print + return counts ───────────────────
# Usage: run_sim <binary> <shots>
# Prints each shot and returns the raw output in $RUN_OUTPUT
run_sim() {
    local bin="$1" shots="$2"
    RUN_OUTPUT=$("$bin" "$shots" 2>/dev/null)
    echo "$RUN_OUTPUT"
}

# ── Helper: count correlated shots (all bits equal) ──────────────────────────
count_correlated() {
    local output="$1"
    # Shot lines: "shot N: b0 b1 b2 ..."  ($1=shot $2=N: $3=first_bit ...)
    echo "$output" | awk '
    /^shot [0-9]+:/ {
        if (NF < 3) next
        first = $3; ok = 1
        for (i = 4; i <= NF; i++) if ($i != first) { ok = 0; break }
        total++; if (ok) corr++
    }
    END { printf "%d/%d", corr, total }'
}

# ── Helper: count shots matching a specific pattern ───────────────────────────
count_pattern() {
    local output="$1" pattern="$2"
    echo "$output" | grep -c "shot [0-9]*: ${pattern}" || true
}

# ══════════════════════════════════════════════════════════════════════════════
# 1. Bell state (ideal)
# ══════════════════════════════════════════════════════════════════════════════
banner "1. Bell state — ideal  (expect 100% correlated)"

BIN=$(build_circuit "bell_ideal" "${REPO_ROOT}/test/Integration/bell-state-ft.mlir")
run_sim "$BIN" "$SHOTS"
CORR=$(count_correlated "$RUN_OUTPUT")
TOTAL=${CORR#*/}; CORR_N=${CORR%/*}
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${CORR_N}/${TOTAL} }")
echo ""
if [ "$CORR_N" -eq "$TOTAL" ]; then
    ok "Correlation: ${CORR}  (${PCT}%)  — PASS"
else
    warn "Correlation: ${CORR}  (${PCT}%)  — expected 100%"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 2. GHZ-3 (ideal)
# ══════════════════════════════════════════════════════════════════════════════
banner "2. GHZ-3 — ideal  (expect 100% correlated)"

# Build inline MLIR
GHZ3_MLIR="${WORKDIR}/ghz3.mlir"
cat > "$GHZ3_MLIR" << 'MLIR'
module {
  func.func @ghz3() -> (i1, i1, i1) {
    %q0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %q1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %q2 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %h0 = ftqc.logical_h %q0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %c0, %t1 = ftqc.logical_cnot %h0, %q1
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
           !ftqc.logical_qubit<#ftqc.ecc<steane>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
            !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    %c1, %t2 = ftqc.logical_cnot %t1, %q2
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
           !ftqc.logical_qubit<#ftqc.ecc<steane>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
            !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    %m0 = ftqc.logical_measure %c0
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    %m1 = ftqc.logical_measure %c1
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    %m2 = ftqc.logical_measure %t2
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    return %m0, %m1, %m2 : i1, i1, i1
  }
}
MLIR

BIN=$(build_circuit "ghz3_ideal" "$GHZ3_MLIR")
run_sim "$BIN" "$SHOTS"
CORR=$(count_correlated "$RUN_OUTPUT")
TOTAL=${CORR#*/}; CORR_N=${CORR%/*}
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${CORR_N}/${TOTAL} }")
echo ""
if [ "$CORR_N" -eq "$TOTAL" ]; then
    ok "Correlation: ${CORR}  (${PCT}%)  — PASS"
else
    warn "Correlation: ${CORR}  (${PCT}%)  — expected 100%"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 3. GHZ-4 (ideal, using existing test MLIR)
# ══════════════════════════════════════════════════════════════════════════════
banner "3. GHZ-4 — ideal  (expect 100% correlated)"

BIN=$(build_circuit "ghz4_ideal" "${REPO_ROOT}/test/Integration/ghz4-steane.mlir")
run_sim "$BIN" "$SHOTS"
CORR=$(count_correlated "$RUN_OUTPUT")
TOTAL=${CORR#*/}; CORR_N=${CORR%/*}
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${CORR_N}/${TOTAL} }")
echo ""
if [ "$CORR_N" -eq "$TOTAL" ]; then
    ok "Correlation: ${CORR}  (${PCT}%)  — PASS"
else
    warn "Correlation: ${CORR}  (${PCT}%)  — expected 100%"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 4. Grover 2-qubit (ideal, target |11>)
# ══════════════════════════════════════════════════════════════════════════════
banner "4. Grover 2-qubit — ideal  (expect 100% outcome 1 1)"

BIN=$(build_circuit "grover_ideal" "${REPO_ROOT}/test/Integration/grover-ft.mlir")
run_sim "$BIN" "$SHOTS"
N11=$(count_pattern "$RUN_OUTPUT" "1 1")
echo ""
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${N11}/${SHOTS} }")
if [ "$N11" -eq "$SHOTS" ]; then
    ok "|11> hits: ${N11}/${SHOTS}  (${PCT}%)  — PASS"
else
    warn "|11> hits: ${N11}/${SHOTS}  (${PCT}%)  — expected 100%"
fi

# ══════════════════════════════════════════════════════════════════════════════
# 5. Bell — moderate noise  (T1=5, T2=2)
# ══════════════════════════════════════════════════════════════════════════════
banner "5. Bell — moderate noise  T1=5 T2=2  (some errors expected)"

BIN=$(build_circuit "bell_noise_mod" "${REPO_ROOT}/test/Integration/bell-state-ft.mlir" \
      "with-noise=1 t1=5.0 t2=2.0 rng-seed=42")
run_sim "$BIN" "$SHOTS"
CORR=$(count_correlated "$RUN_OUTPUT")
TOTAL=${CORR#*/}; CORR_N=${CORR%/*}
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${CORR_N}/${TOTAL} }")
ERRORS=$(( TOTAL - CORR_N ))
echo ""
ok "Correlation: ${CORR}  (${PCT}%)  |  Errors: ${ERRORS}/${TOTAL}"

# ══════════════════════════════════════════════════════════════════════════════
# 6. Bell — aggressive noise  (T1=0.5, T2=0.2)
# ══════════════════════════════════════════════════════════════════════════════
banner "6. Bell — aggressive noise  T1=0.5 T2=0.2  (clear decoherence)"

BIN=$(build_circuit "bell_noise_agg" "${REPO_ROOT}/test/Integration/bell-state-ft.mlir" \
      "with-noise=1 t1=0.5 t2=0.2 rng-seed=7")
run_sim "$BIN" "$SHOTS"
CORR=$(count_correlated "$RUN_OUTPUT")
TOTAL=${CORR#*/}; CORR_N=${CORR%/*}
PCT=$(awk "BEGIN { printf \"%.1f\", 100*${CORR_N}/${TOTAL} }")
ERRORS=$(( TOTAL - CORR_N ))
echo ""
ok "Correlation: ${CORR}  (${PCT}%)  |  Errors: ${ERRORS}/${TOTAL}"
if [ "$ERRORS" -gt 0 ]; then
    ok "Decoherence visible  — PASS"
else
    warn "No errors observed — noise may be too weak for this circuit depth"
fi

# ══════════════════════════════════════════════════════════════════════════════
# Summary
# ══════════════════════════════════════════════════════════════════════════════
banner "Summary"
echo -e "  All circuits compiled and ran against Intel-QS ${B}libiqs.so${N}"
echo -e "  Circuits tested  : Bell (ideal+noisy), GHZ-3, GHZ-4, Grover"
echo -e "  Gate API used    : ApplyHadamard, ApplyCPauliX/Z, ApplyT, ApplyToffoli"
echo -e "  Noise API used   : NoisyQureg<ComplexDP> with ApplyNoiseGatesOnAllQubits"
echo -e "  Work dir         : $WORKDIR"
echo ""
echo -e "  To recompile from scratch:"
echo -e "    rm -rf $WORKDIR && $0"
echo ""
