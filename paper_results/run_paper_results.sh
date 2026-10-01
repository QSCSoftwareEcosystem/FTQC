#!/usr/bin/env bash
# =============================================================================
# run_paper_results.sh — Reproduce all results for latex/ftqc-paper.tex
#
# Usage (run from qftc/ directory):
#   bash paper_results/run_paper_results.sh [--skip-pennylane] [--skip-build] [--plots-only]
#
# Flags:
#   --plots-only     Skip build/tests/sims (steps 1-6,9-10); regenerate PDFs/PNGs only.
#                    Uses existing stim_results.csv from paper_results/ or repo root.
#   --skip-build     Skip cmake rebuild (use existing ftqc-opt binary)
#   --skip-pennylane Skip PennyLane step (requires pen.venv, ~10 min)
#
# Output files written to paper_results/:
#   Logs      : 01_build.log, 02_lit_tests.log, 03_sim_qasm3.log,
#               04_sim_qsim.log, 05_sim_steane_aer.log, 06_sim_stim.log,
#               07_plot_stim.log, 08_sim_pennylane.log,
#               09_scale_experiment.log, 10_latex.log
#   Data      : stim_results.csv, pennylane_results.csv,
#               scale_data.csv, scale_plots.tex
#   PDFs (vector) → qftc/figures/
#     fig1_pL_vs_p_loglog.pdf  fig2_suppression.pdf  fig3_scalability.pdf
#     fig4_per_benchmark.pdf   fig5_composite_summary.pdf
#     figA_pl_vs_stim_loglog.pdf  figB_all_methods.pdf
#     figC_scalability.pdf        figD_summary_panel.pdf
#   PNGs (raster) → paper_results/
#     fig1_pL_vs_p_loglog.png  fig2_suppression.png  fig3_scalability.png
#     fig4_per_benchmark.png   fig5_composite_summary.png
#     figA_pl_vs_stim_loglog.png  figB_all_methods.png
#     figC_scalability.png        figD_summary_panel.png
#   Paper     : ftqc-paper.pdf
# =============================================================================

set -uo pipefail
set -x

# ── Paths ─────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QFTC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR"
FIGURES_DIR="$QFTC_DIR/figures"

[ -n "$VIRTUAL_ENV" ] && IS_VENV=true || IS_VENV=false
VENV="${VIRTUAL_ENV:-none}"
if [ "$IS_VENV" = true ]; then
QASM_PY="$VENV/bin/python"
QSIM_PY="$VENV/bin/python"
else
QASM_PY="python"
QSIM_PY="python"
fi
PEN_PY="/home/nqx/Quantum/quantum-resources/qiree/pen.venv/bin/python"
FTQC_OPT="$QFTC_DIR/build/src/ftqc-opt"
# LLVM/MLIR install prefix (same default as build.sh); exported so the build.sh
# invocation in step 1 inherits the same value.
export LLVM="${LLVM:-${QFTC_DIR}/../llvm-project/install}"
# llvm-lit usually lives in the LLVM build tree (sibling of the install prefix).
LIT="${LIT:-}"
if [[ -z "$LIT" ]]; then
  for c in "$LLVM/bin/llvm-lit" "$LLVM/../build/bin/llvm-lit" \
           "$(command -v llvm-lit lit 2>/dev/null | head -1)"; do
    [[ -x "$c" ]] && LIT="$c" && break
  done
fi
export LD_LIBRARY_PATH="$QFTC_DIR/install/lib:$LLVM/lib:${LD_LIBRARY_PATH:-}"

# ── Option flags ──────────────────────────────────────────────────────────────
SKIP_PENNYLANE=0
SKIP_SCALABILITY=0
SKIP_BUILD=0
PLOTS_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --skip-scalability) SKIP_SCALABILITY=1 ;;
        --skip-pennylane) SKIP_PENNYLANE=1 ;;
        --skip-build)     SKIP_BUILD=1     ;;
        --plots-only)     PLOTS_ONLY=1; SKIP_BUILD=1 ;;
    esac
done

# ── Helper ────────────────────────────────────────────────────────────────────
step() { echo; echo "================================================================="; echo "  $*"; echo "================================================================="; }
ok()   { echo "  [OK] $*"; }
warn() { echo "  [WARN] $*"; }

# ── Validate environment ───────────────────────────────────────────────────────
step "Checking environment"
[[ -x "$QASM_PY"  ]] || { echo "ERROR: $VENV not found at $QASM_PY"; exit 1; }
[[ -x "$QSIM_PY"  ]] || { echo "ERROR: $VENV not found at $QSIM_PY"; exit 1; }
[[ -f "$LIT"      ]] || { echo "ERROR: llvm-lit not found at $LIT"; exit 1; }
if [[ $SKIP_PENNYLANE -eq 0 ]]; then
    [[ -x "$PEN_PY" ]] || { warn "pen.venv not found — skipping PennyLane step (use --skip-pennylane to suppress)"; SKIP_PENNYLANE=1; }
fi
ok "Environment OK"

# Switch to qftc/ for all script invocations
cd "$QFTC_DIR"

# Pre-create output directories so plots never fail on mkdir
mkdir -p "$RESULTS_DIR" "$FIGURES_DIR"

# =============================================================================
# Step 1 — Build ftqc-opt
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 1/10 — Build (skipped via --plots-only)"
elif [[ $SKIP_BUILD -eq 0 ]]; then
    step "Step 1/10 — Build ftqc-opt"
    cmake --build build --target ftqc-opt -j"$(nproc)" \
        2>&1 | tee "$RESULTS_DIR/01_build.log" || warn "Build failed — check 01_build.log"
    ok "ftqc-opt built: $FTQC_OPT"
else
    step "Step 1/10 — Build (skipped via --skip-build)"
    [[ -x "$FTQC_OPT" ]] || warn "$FTQC_OPT not found; simulation steps may fail"
fi

# =============================================================================
# Step 2 — Lit/FileCheck regression suite (87 tests)
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 2/10 — Lit tests (skipped via --plots-only)"
else
    step "Step 2/10 — Lit regression tests"
    "$LIT" \
        build/test/Transforms \
        build/test/Dialect \
        build/test/Analysis \
        build/test/Integration \
        -j"$(nproc)" \
        2>&1 | tee "$RESULTS_DIR/02_lit_tests.log" || warn "Some lit tests failed — check 02_lit_tests.log"
    ok "Lit tests logged to 02_lit_tests.log"
fi

# =============================================================================
# Step 3 — Qiskit Aer simulation: QASM3 back-end correctness (4 circuits)
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 3/10 — Qiskit Aer simulation (skipped via --plots-only)"
else
    step "Step 3/10 — Qiskit Aer simulation (sim_qasm3.py)"
    "$QASM_PY" utils/sim_qasm3.py \
        2>&1 | tee "$RESULTS_DIR/03_sim_qasm3.log" || warn "sim_qasm3.py failed — check 03_sim_qasm3.log"
    ok "Aer results → 03_sim_qasm3.log"
fi

# =============================================================================
# Step 4 — qsim simulation: Grover-2 and QFT-3 independent verification
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 4/10 — qsim simulation (skipped via --plots-only)"
else
    step "Step 4/10 — qsim simulation (sim_qsim.py)"
    "$QSIM_PY" utils/sim_qsim.py \
        2>&1 | tee "$RESULTS_DIR/04_sim_qsim.log" || warn "sim_qsim.py failed — check 04_sim_qsim.log"
    ok "qsim results → 04_sim_qsim.log"
fi

# =============================================================================
# Step 5 — Steane physical-circuit Aer simulation (14-qubit Bell, 28-qubit GHZ)
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 5/10 — Steane simulation (skipped via --plots-only)"
else
    step "Step 5/10 — Steane physical-circuit simulation (sim_steane_aer.py)"
    "$QASM_PY" utils/sim_steane_aer.py \
        2>&1 | tee "$RESULTS_DIR/05_sim_steane_aer.log" || warn "sim_steane_aer.py failed — check 05_sim_steane_aer.log"
    ok "Steane Aer results → 05_sim_steane_aer.log"
fi

# =============================================================================
# Step 6 — STIM QEC Monte Carlo sweep (4 benchmarks × 18 error rates × 5000 shots)
#   Output: paper_results/stim_results.csv
# =============================================================================
if [[ $PLOTS_ONLY -eq 1 ]]; then
    step "Step 6/10 — STIM sweep (skipped via --plots-only)"
    # Fall back to existing CSV in repo root if available
    if [[ ! -f "$RESULTS_DIR/stim_results.csv" ]] && [[ -f "$QFTC_DIR/stim_results.csv" ]]; then
        cp "$QFTC_DIR/stim_results.csv" "$RESULTS_DIR/stim_results.csv"
        ok "Copied existing stim_results.csv from repo root"
    fi
else
    step "Step 6/10 — STIM QEC simulation sweep (sim_stim.py)"
    "$QASM_PY" utils/sim_stim.py \
        --shots 5000 \
        --output "$RESULTS_DIR/stim_results.csv" \
        2>&1 | tee "$RESULTS_DIR/06_sim_stim.log" || warn "sim_stim.py failed — check 06_sim_stim.log"
    ok "STIM results → stim_results.csv"
fi

# =============================================================================
# Step 7 — STIM result figures (5 separate plot files)
#   fig1_pL_vs_p_loglog.pdf     — logical error rate vs physical (log-log)
#   fig2_suppression.pdf        — suppression factor p_L/p vs p
#   fig3_scalability.pdf        — physical qubit count scalability bars
#   fig4_per_benchmark.pdf      — per-benchmark with power-law fits
#   fig5_composite_summary.pdf  — 2×2 composite summary panel
# =============================================================================
step "Step 7/10 — STIM result plots (plot_stim_results.py)"
if [[ ! -f "$RESULTS_DIR/stim_results.csv" ]]; then
    warn "stim_results.csv not found — skipping STIM plots (run step 6 first or use --plots-only with existing CSV)"
else
    "$QASM_PY" utils/plot_stim_results.py \
        --input  "$RESULTS_DIR/stim_results.csv" \
        --outdir "$RESULTS_DIR" \
        --figdir "$FIGURES_DIR" \
        2>&1 | tee "$RESULTS_DIR/07_plot_stim.log"
    ok "STIM PDFs → $FIGURES_DIR/  (fig1–fig5)"
    ok "STIM PNGs → $RESULTS_DIR/  (fig1–fig5)"
fi

# =============================================================================
# Step 8 — PennyLane density-matrix QEC comparison + figures (4 separate files)
#   figA_pl_vs_stim_loglog.pdf  — PennyLane vs STIM log-log comparison
#   figB_all_methods.pdf        — all methods overlay
#   figC_scalability.pdf        — scalability: DM vs stabilizer
#   figD_summary_panel.pdf      — 2×2 summary panel
#   pennylane_results.csv       — PennyLane raw p_L data
# =============================================================================
if [[ $SKIP_PENNYLANE -eq 0 ]]; then
    step "Step 8/10 — PennyLane QEC comparison (sim_pennylane_qec.py)"
    "$PEN_PY" utils/sim_pennylane_qec.py \
        --outdir "$RESULTS_DIR" \
        --figdir "$FIGURES_DIR" \
        --stim-csv "$RESULTS_DIR/stim_results.csv" \
        2>&1 | tee "$RESULTS_DIR/08_sim_pennylane.log"
    ok "PennyLane PDFs → $FIGURES_DIR/  (figA–figD)"
    ok "PennyLane PNGs → $RESULTS_DIR/  (figA–figD)"
    ok "PennyLane data → pennylane_results.csv"
else
    step "Step 8/10 — PennyLane QEC comparison (skipped)"
    warn "Add pen.venv and remove --skip-pennylane to run this step."
    warn "Expected outputs: figA–figD .pdf/.png, pennylane_results.csv"
fi

# =============================================================================
# Step 9 — Scaling experiment (physical qubit & compile-time scaling)
#   latex/scale_data.csv    — raw scaling numbers
#   latex/scale_plots.tex   — pgfplots coordinates included by the paper
# =============================================================================
if [[ $SKIP_SCALABILITY -eq 0 ]]; then
step "Step 9/10 — Scaling experiment (scale_experiment.py)"
"$QASM_PY" utils/scale_experiment.py \
    2>&1 | tee "$RESULTS_DIR/09_scale_experiment.log"
# Copy generated files into paper_results/ for archival
cp latex/scale_data.csv  "$RESULTS_DIR/scale_data.csv"
cp latex/scale_plots.tex "$RESULTS_DIR/scale_plots.tex"
fi
ok "Scaling data → scale_data.csv, scale_plots.tex"

# =============================================================================
# Summary
# =============================================================================
step "All steps complete"
echo
echo "Output directory: $RESULTS_DIR"
echo
FIGURES_DIR="$QFTC_DIR/figures"
echo "  Plot PDFs (vector) → $FIGURES_DIR/"
ls "$FIGURES_DIR"/fig*.pdf 2>/dev/null | sort | while read f; do printf "    %s\n" "$(basename "$f")"; done || echo "    (none)"
echo "  Plot PNGs (raster) → $RESULTS_DIR/"
ls "$RESULTS_DIR"/fig*.png 2>/dev/null | sort | while read f; do printf "    %s\n" "$(basename "$f")"; done || echo "    (none)"
echo
echo "  Data files:"
ls "$RESULTS_DIR"/*.csv "$RESULTS_DIR"/*.tex 2>/dev/null | while read f; do printf "    %s\n" "$(basename "$f")"; done || echo "    (none)"
echo
echo "  Paper PDF:  ftqc-paper.pdf"
echo "  Build log:  01_build.log"
echo "  Test log:   02_lit_tests.log"
echo "  Sim logs:   03–08_*.log"
echo "  LaTeX logs: 10_latex_pass1.log  10_latex_pass2.log"
echo
