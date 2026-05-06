#!/usr/bin/env python3
"""sim_stim_ftqc_loop.py — Compiler-driven QEC simulation via STIM.

Replaces the hand-built Python SteaneCircuit in sim_stim.py with a real call to
the FTQC compiler (ftqc-opt) for every (benchmark, p) sweep point.

Workflow per point
------------------
  1. ftqc-opt --ftqc-insert-syndrome --ftqc-transversal-gates
              --ftqc-lower-to-stim=physical-error-rate=p
                                    syndrome-rounds=<R>
                                    include-comments=false
              <benchmark.mlir>
  2. Parse the Stim circuit from stderr.
  3. Build detector-error model; decode with PyMatching MWPM.
  4. Report per-shot failure probability p_shot.

The sweep is otherwise identical to sim_stim.py (same error rates, same shots,
same CSV schema, same ASCII plots) so results can be directly compared.

Usage
-----
    qasm.venv/bin/python sim_stim_ftqc_loop.py [options]

Options
-------
    --shots N             Shots per (benchmark, p) point  [default: 5000]
    --syndrome-rounds R   Syndrome rounds per logical gate [default: 3]
    --output FILE         CSV output path                 [default: stim_ftqc_results.csv]
    --ftqc-opt PATH       Override path to ftqc-opt binary
    --dry-run             Print compiled circuits, skip Monte Carlo
"""

import sys, os, csv, math, argparse, subprocess, time
import stim
import pymatching
import numpy as np

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

DEFAULT_FTQC_OPT = os.path.join(_SCRIPT_DIR, "build/src/ftqc-opt")

# MLIR benchmark files (relative to _SCRIPT_DIR)
BENCHMARKS = {
    "Bell":   ("test/Integration/bell-state-ft.mlir",   2),
    "GHZ":    ("test/Integration/ghz4-steane.mlir",     4),
    "Grover": ("test/Integration/grover-ft.mlir",       2),
    "QFT":    ("test/Integration/qft3-steane.mlir",     3),
}
# (mlir_file, n_logical)

P_THRESHOLD = 0.01   # Steane d=3 depolarising threshold ≈ 1 %

# 18 log-uniform points  1e-4 … 1.5e-2  (identical to sim_stim.py)
ERROR_RATES = np.logspace(-4, -1.82, 18)

# ---------------------------------------------------------------------------
# Compiler invocation
# ---------------------------------------------------------------------------

def compile_stim_circuit(
    ftqc_opt: str,
    mlir_file: str,
    p: float,
    syndrome_rounds: int = 3,
) -> stim.Circuit:
    """Call ftqc-opt and return the parsed Stim circuit.

    Pipeline:
        --ftqc-insert-syndrome        inserts X/Z syndrome ops after every gate
        --ftqc-transversal-gates      decomposes transversal gates (T → magic inject)
        --ftqc-lower-to-stim=...      emits physical Stim circuit to stderr

    Raises RuntimeError if the compiler exits non-zero or emits an empty circuit.
    """
    stim_opts = (
        f"physical-error-rate={p} "
        f"syndrome-rounds={syndrome_rounds} "
        f"include-comments=false"
    )
    cmd = [
        ftqc_opt,
        "--ftqc-insert-syndrome",
        "--ftqc-transversal-gates",
        f"--ftqc-lower-to-stim={stim_opts}",
        mlir_file,
    ]
    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=_SCRIPT_DIR,
        )
    except subprocess.TimeoutExpired:
        raise RuntimeError("ftqc-opt timed out after 30 s")

    if result.returncode != 0:
        snippet = (result.stderr or result.stdout)[:300]
        raise RuntimeError(f"ftqc-opt exited {result.returncode}: {snippet}")

    stim_str = result.stderr.strip()
    if not stim_str:
        raise RuntimeError("ftqc-opt produced no Stim output on stderr")

    try:
        circuit = stim.Circuit(stim_str)
    except Exception as e:
        raise RuntimeError(f"stim.Circuit() parse failed: {e}\n--- output ---\n{stim_str[:400]}")

    return circuit


# ---------------------------------------------------------------------------
# Monte Carlo simulation + MWPM decoding  (identical to sim_stim.py)
# ---------------------------------------------------------------------------

def simulate(circuit: stim.Circuit, num_shots: int) -> float:
    """Run Monte Carlo shots, decode with MWPM, return per-shot failure rate."""
    dem = None
    for kwargs in [
        dict(decompose_errors=True, approximate_disjoint_errors=True),
        dict(approximate_disjoint_errors=True),
        dict(),
    ]:
        try:
            dem = circuit.detector_error_model(**kwargs)
            break
        except Exception:
            pass

    if dem is None or len(dem) == 0 or circuit.num_observables == 0:
        # No decoder possible (e.g. QFT with 0 observables) — sample raw observables.
        det_sampler = circuit.compile_detector_sampler()
        det_samples = det_sampler.sample(num_shots, append_observables=True)
        n_det = circuit.num_detectors
        obs   = det_samples[:, n_det:]
        if obs.shape[1] == 0:
            return float("nan")
        return float(np.mean(obs[:, 0]))

    matcher = pymatching.Matching.from_detector_error_model(dem)
    sampler  = circuit.compile_detector_sampler()
    n_det    = circuit.num_detectors
    samples  = sampler.sample(num_shots, append_observables=True)
    det_evts = samples[:, :n_det]
    obs_flip = samples[:, n_det:]

    if obs_flip.shape[1] == 0:
        return float("nan")

    preds  = matcher.decode_batch(det_evts)
    n_errs = int(np.sum(preds[:, 0] != obs_flip[:, 0]))
    return n_errs / num_shots


# ---------------------------------------------------------------------------
# Main sweep
# ---------------------------------------------------------------------------

def run_sweep(
    ftqc_opt: str,
    num_shots: int,
    syndrome_rounds: int,
    output_csv: str,
    dry_run: bool = False,
):
    print(f"\n{'='*68}")
    print(f"  FTQC-Compiler-Driven STIM QEC Simulation")
    print(f"  Compiler        : {ftqc_opt}")
    print(f"  Shots per point : {num_shots:,}")
    print(f"  Syndrome rounds : {syndrome_rounds} per logical gate")
    print(f"  Threshold (d=3) : p_th ≈ {P_THRESHOLD:.0%}")
    print(f"  Mode            : {'DRY-RUN (no Monte Carlo)' if dry_run else 'full simulation'}")
    print(f"{'='*68}\n")

    all_results = []

    for bench_name, (mlir_file, n_logical) in BENCHMARKS.items():
        abs_mlir = os.path.join(_SCRIPT_DIR, mlir_file)
        if not os.path.exists(abs_mlir):
            print(f"  [{bench_name}] MLIR file not found: {mlir_file} — skipping.\n")
            continue

        print(f"  Benchmark : {bench_name}  (n_logical={n_logical}, mlir={mlir_file})")

        # Compile once at a nominal p to report circuit structure.
        try:
            probe = compile_stim_circuit(ftqc_opt, abs_mlir, p=1e-3,
                                         syndrome_rounds=syndrome_rounds)
            n_phys = probe.num_qubits
            print(f"  Circuit   : {n_phys} physical qubits | "
                  f"{probe.num_detectors} detectors | "
                  f"{probe.num_observables} observables")
            if probe.num_observables == 0:
                print(f"  WARNING   : 0 observables — p_shot will be NaN "
                      f"(OBSERVABLE_INCLUDE not emitted by compiler for this circuit)")
        except RuntimeError as e:
            print(f"  ERROR compiling {bench_name} at probe p: {e}\n")
            continue

        if dry_run:
            print(f"\n  --- Stim circuit (p=1e-3, first 20 lines) ---")
            for line in str(probe).splitlines()[:20]:
                print(f"    {line}")
            print(f"  ...\n")
            continue

        print(f"\n  {'p':>10}  {'p_shot':>10}  {'p_shot/p':>9}  "
              f"{'compile':>8}  {'simulate':>9}")
        print(f"  {'-'*55}")

        for p in ERROR_RATES:
            t_compile = time.time()
            try:
                circuit = compile_stim_circuit(ftqc_opt, abs_mlir, p,
                                               syndrome_rounds=syndrome_rounds)
            except RuntimeError as e:
                print(f"  {p:>10.2e}  {'COMPILE ERR':>10}  {str(e)[:35]}")
                continue
            t_compiled = time.time() - t_compile

            t_sim = time.time()
            try:
                p_shot = simulate(circuit, num_shots)
            except Exception as e:
                print(f"  {p:>10.2e}  {'SIM ERR':>10}  {str(e)[:35]}")
                continue
            t_simulated = time.time() - t_sim

            ratio = p_shot / p if (p > 0 and not math.isnan(p_shot)) else float("nan")
            p_str = f"{p_shot:.4e}" if not math.isnan(p_shot) else "  nan   "
            r_str = f"{ratio:.2f}" if not math.isnan(ratio) else "  nan "
            print(f"  {p:>10.2e}  {p_str:>10}  {r_str:>9}  "
                  f"{t_compiled*1000:>6.1f}ms  {t_simulated:>7.2f}s")

            all_results.append({
                "benchmark": bench_name,
                "n_logical": n_logical,
                "n_phys":    circuit.num_qubits,
                "p":         float(p),
                "p_shot":    float(p_shot) if not math.isnan(p_shot) else None,
                "detectors": circuit.num_detectors,
                "observables": circuit.num_observables,
            })

        print()

    if dry_run or not all_results:
        return

    # Write CSV
    with open(output_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "benchmark", "n_logical", "n_phys", "p", "p_shot",
            "detectors", "observables",
        ])
        writer.writeheader()
        writer.writerows(all_results)
    print(f"  Results saved to: {output_csv}\n")

    print_ascii_plots(all_results)
    print_summary_table(all_results)


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------

def print_summary_table(results):
    """Print Table-IV-style summary at p ≈ 1e-3."""
    target_p = 1e-3
    print(f"{'='*68}")
    print(f"  Summary at p ≈ 1e-3 (compiler-driven, syndrome-rounds=3)")
    print(f"  {'Benchmark':<12} {'n_log':>6} {'n_phys':>7} "
          f"{'detectors':>10} {'observables':>12} {'p_shot':>10}")
    print(f"  {'-'*60}")
    by_bench = {}
    for row in results:
        by_bench.setdefault(row["benchmark"], []).append(row)
    for bench, rows in by_bench.items():
        closest = min(rows, key=lambda r: abs(r["p"] - target_p))
        p_str = (f"{closest['p_shot']:.4e}"
                 if closest["p_shot"] is not None else "   nan  ")
        print(f"  {bench:<12} {closest['n_logical']:>6} {closest['n_phys']:>7} "
              f"{closest['detectors']:>10} {closest['observables']:>12} {p_str:>10}")
    print()


def print_ascii_plots(results):
    W, H = 55, 14
    print(f"{'='*68}")
    print(f"  ASCII plots: p_shot (Y) vs p (X), log-log scale")
    print(f"  '*' = simulated,  '-' = p_shot = p line")
    print(f"{'='*68}\n")

    by_bench = {}
    for row in results:
        if row["p_shot"] is not None:
            by_bench.setdefault(row["benchmark"], []).append(
                (row["p"], row["p_shot"]))

    for bench, pts in by_bench.items():
        ps, pLs = zip(*pts) if pts else ([], [])
        valid = [v for v in pLs if v > 0]
        if not ps or not valid:
            continue
        lp_min = math.log10(min(ps))
        lp_max = math.log10(max(ps))
        lL_min = math.log10(min(valid))
        lL_max = math.log10(max(valid))
        if lL_min >= lL_max:
            lL_min = lL_max - 3

        grid = [["." for _ in range(W)] for _ in range(H)]

        for p, pL in pts:
            if pL <= 0:
                continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W - 1))
            r = H - 1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H - 1))
            c = max(0, min(W - 1, c))
            r = max(0, min(H - 1, r))
            grid[r][c] = "*"

        for p in np.logspace(lp_min, lp_max, W):
            pL = p
            if pL <= 0 or not (lL_min <= math.log10(pL) <= lL_max):
                continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W - 1))
            r = H - 1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H - 1))
            c = max(0, min(W - 1, c))
            r = max(0, min(H - 1, r))
            if grid[r][c] == ".":
                grid[r][c] = "-"

        n_log = BENCHMARKS[bench][1]
        print(f"  {bench} (n_logical={n_log}) | "
              f"Y: p_shot ∈ [{10**lL_min:.1e},{10**lL_max:.1e}] | "
              f"X: p ∈ [{10**lp_min:.1e},{10**lp_max:.1e}]")
        for row in grid:
            print("  |" + "".join(row) + "|")
        print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--shots",           type=int,  default=5_000,
                        help="Monte Carlo shots per (benchmark, p) point")
    parser.add_argument("--syndrome-rounds", type=int,  default=3,
                        help="Syndrome rounds per logical gate passed to --ftqc-lower-to-stim")
    parser.add_argument("--output",          default="stim_ftqc_results.csv",
                        help="CSV output path")
    parser.add_argument("--ftqc-opt",        default=DEFAULT_FTQC_OPT,
                        help="Path to ftqc-opt binary")
    parser.add_argument("--dry-run",         action="store_true",
                        help="Compile and print circuits; skip Monte Carlo")
    args = parser.parse_args()

    ftqc_opt = args.ftqc_opt
    if not os.path.exists(ftqc_opt):
        print(f"ERROR: ftqc-opt not found at '{ftqc_opt}'.")
        print(f"  Build it with:  cd qftc && cmake --build build --target ftqc-opt")
        print(f"  Or pass:        --ftqc-opt /path/to/ftqc-opt")
        sys.exit(1)

    os.chdir(_SCRIPT_DIR)
    run_sweep(
        ftqc_opt=ftqc_opt,
        num_shots=args.shots,
        syndrome_rounds=args.syndrome_rounds,
        output_csv=args.output,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
