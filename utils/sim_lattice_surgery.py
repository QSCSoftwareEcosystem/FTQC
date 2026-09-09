#!/usr/bin/env python3
"""sim_lattice_surgery.py — Surface-code lattice surgery QEC simulation via STIM.

Calls ftqc-opt --ftqc-insert-syndrome --ftqc-transversal-gates
      --ftqc-lower-to-lattice-surgery=physical-error-rate=p
                                      syndrome-rounds=R
                                      include-comments=false
for each (benchmark, p) sweep point, parses the Stim circuit, and runs
Monte Carlo simulation with MWPM decoding via PyMatching.

Observable convention (consistent for all benchmarks):
  All logical qubits' left-column measurements are XOR-ed into a single
  combined parity.  For a correct circuit this is always 0:
    Bell   : Z_ctrl XOR Z_tgt = 0 (correlated)
    GHZ-4  : Z_q0 XOR ... XOR Z_q3 = 0 (all equal, count even=4)
    Grover : Z_q0 XOR Z_q1 = 1 XOR 1 = 0 (both qubits → |1>)
    QFT    : skipped (0 observables in current compiler output)

Usage
-----
    qasm.venv/bin/python sim_lattice_surgery.py [options]
"""

import sys, os, csv, math, argparse, subprocess, time
import stim
import pymatching
import numpy as np

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

DEFAULT_FTQC_OPT = os.path.join(os.path.dirname(_SCRIPT_DIR), "build/src/ftqc-opt")

BENCHMARKS = {
    "Bell":   ("test/Integration/bell-state-ft.mlir",   2),
    "GHZ":    ("test/Integration/ghz4-steane.mlir",     4),
    "Grover": ("test/Integration/grover-ft.mlir",       2),
    "QFT":    ("test/Integration/qft3-steane.mlir",     3),
}

P_THRESHOLD  = 0.01
ERROR_RATES  = np.logspace(-4, -1.82, 18)


# ---------------------------------------------------------------------------
# Compiler invocation
# ---------------------------------------------------------------------------

def compile_circuit(ftqc_opt, mlir_file, p, syndrome_rounds=1):
    """Call ftqc-opt with the lattice-surgery pass and return a stim.Circuit."""
    ls_opts = (
        f"physical-error-rate={p} "
        f"syndrome-rounds={syndrome_rounds} "
        f"include-comments=false"
    )
    cmd = [
        ftqc_opt,
        "--ftqc-insert-syndrome",
        "--ftqc-transversal-gates",
        f"--ftqc-lower-to-lattice-surgery={ls_opts}",
        mlir_file,
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30, cwd=_SCRIPT_DIR)
    except subprocess.TimeoutExpired:
        raise RuntimeError("ftqc-opt timed out")

    if result.returncode != 0:
        raise RuntimeError(
            f"ftqc-opt exit {result.returncode}: {result.stderr[:300]}")

    stim_str = result.stderr.strip()
    if not stim_str:
        raise RuntimeError("ftqc-opt produced no Stim output on stderr")

    return stim.Circuit(stim_str)


# ---------------------------------------------------------------------------
# Monte Carlo simulation with combined-parity observable
# ---------------------------------------------------------------------------

def simulate(circuit: stim.Circuit, num_shots: int) -> float:
    """Run Monte Carlo shots with MWPM decoding.

    Uses the combined-parity observable: XOR of all per-qubit logical Z
    measurements.  For all supported benchmarks this parity is deterministically
    0 for a perfect circuit:
      Bell  : Z_ctrl XOR Z_tgt = 0  (always correlated)
      GHZ-4 : XOR of four equal bits = 0  (count is even=4)
      Grover: Z_q0=1, Z_q1=1  →  1 XOR 1 = 0  (marked state |11>)
    """
    if circuit.num_observables == 0:
        return float("nan")

    # Build DEM — try multiple decomposition strategies.
    dem = None
    for kwargs in [
        dict(decompose_errors=True, approximate_disjoint_errors=True),
        dict(approximate_disjoint_errors=True),
        dict(),
    ]:
        try:
            dem = circuit.detector_error_model(**kwargs)
            if dem is not None and len(dem) > 0:
                break
        except Exception:
            pass

    if dem is None or len(dem) == 0:
        return float("nan")

    matcher = pymatching.Matching.from_detector_error_model(dem)
    sampler  = circuit.compile_detector_sampler()
    n_det    = circuit.num_detectors
    samples  = sampler.sample(num_shots, append_observables=True)
    det_evts = samples[:, :n_det]
    obs_flip = samples[:, n_det:]

    if obs_flip.shape[1] == 0:
        return float("nan")

    preds = matcher.decode_batch(det_evts)

    # Combined-parity check: XOR across all logical-qubit observables.
    # Decoder errors = shots where predicted parity ≠ actual parity.
    actual_parity = np.bitwise_xor.reduce(obs_flip.astype(bool), axis=1)
    pred_parity   = np.bitwise_xor.reduce(preds.astype(bool),    axis=1)

    n_errs = int(np.sum(actual_parity != pred_parity))
    return n_errs / num_shots


# ---------------------------------------------------------------------------
# Main sweep
# ---------------------------------------------------------------------------

def run_sweep(ftqc_opt, num_shots, syndrome_rounds, output_csv, dry_run=False):
    print(f"\n{'='*68}")
    print(f"  Surface-Code Lattice Surgery STIM QEC Simulation")
    print(f"  Compiler        : {ftqc_opt}")
    print(f"  Shots per point : {num_shots:,}")
    print(f"  Syndrome rounds : {syndrome_rounds}")
    print(f"  Mode            : {'DRY-RUN' if dry_run else 'full simulation'}")
    print(f"{'='*68}\n")

    all_results = []

    for bench_name, (mlir_file, n_logical) in BENCHMARKS.items():
        abs_mlir = os.path.join(_SCRIPT_DIR, mlir_file)
        if not os.path.exists(abs_mlir):
            print(f"  [{bench_name}] MLIR not found: {mlir_file} — skipping.\n")
            continue

        print(f"  Benchmark : {bench_name}  (n_logical={n_logical})")

        try:
            probe = compile_circuit(ftqc_opt, abs_mlir, p=1e-3,
                                    syndrome_rounds=syndrome_rounds)
            n_phys = probe.num_qubits
            print(f"  Circuit   : {n_phys} physical qubits | "
                  f"{probe.num_detectors} detectors | "
                  f"{probe.num_observables} observables")
            if probe.num_observables == 0:
                print(f"  WARNING   : 0 observables — p_shot will be NaN\n")
        except RuntimeError as e:
            print(f"  ERROR: {e}\n")
            continue

        if dry_run:
            print(f"  --- Stim circuit (p=1e-3, first 15 lines) ---")
            for line in str(probe).splitlines()[:15]:
                print(f"    {line}")
            print()
            continue

        print(f"\n  {'p':>10}  {'p_shot':>10}  {'p_shot/p':>9}  "
              f"{'compile':>8}  {'simulate':>9}")
        print(f"  {'-'*55}")

        for p in ERROR_RATES:
            t0 = time.time()
            try:
                circuit = compile_circuit(ftqc_opt, abs_mlir, p,
                                          syndrome_rounds=syndrome_rounds)
            except RuntimeError as e:
                print(f"  {p:>10.2e}  {'COMPILE ERR':>10}  {str(e)[:35]}")
                continue
            t_compile = time.time() - t0

            t0 = time.time()
            try:
                p_shot = simulate(circuit, num_shots)
            except Exception as e:
                print(f"  {p:>10.2e}  {'SIM ERR':>10}  {str(e)[:35]}")
                continue
            t_sim = time.time() - t0

            ratio  = p_shot / p if (p > 0 and not math.isnan(p_shot)) else float("nan")
            p_str  = f"{p_shot:.4e}" if not math.isnan(p_shot) else "  nan   "
            r_str  = f"{ratio:.2f}"  if not math.isnan(ratio)  else "  nan "
            print(f"  {p:>10.2e}  {p_str:>10}  {r_str:>9}  "
                  f"{t_compile*1000:>6.1f}ms  {t_sim:>7.2f}s")

            all_results.append({
                "benchmark":   bench_name,
                "n_logical":   n_logical,
                "n_phys":      circuit.num_qubits,
                "p":           float(p),
                "p_shot":      float(p_shot) if not math.isnan(p_shot) else None,
                "detectors":   circuit.num_detectors,
                "observables": circuit.num_observables,
            })

        print()

    if dry_run or not all_results:
        return

    with open(output_csv, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "benchmark", "n_logical", "n_phys", "p", "p_shot",
            "detectors", "observables"])
        writer.writeheader()
        writer.writerows(all_results)
    print(f"  Results saved to: {output_csv}\n")

    _print_ascii_plots(all_results)
    _print_summary(all_results)


def _print_ascii_plots(results):
    W, H = 55, 12
    by_bench = {}
    for row in results:
        if row["p_shot"] is not None:
            by_bench.setdefault(row["benchmark"], []).append(
                (row["p"], row["p_shot"]))

    print(f"{'='*68}")
    print(f"  ASCII plots: p_shot (Y) vs p (X), log-log  '*'=sim  '-'=p_shot=p")
    print(f"{'='*68}\n")

    for bench, pts in by_bench.items():
        ps, pLs = zip(*pts) if pts else ([], [])
        valid = [v for v in pLs if v > 0]
        if not ps or not valid:
            continue
        lp_min = math.log10(min(ps));  lp_max = math.log10(max(ps))
        lL_min = math.log10(min(valid)); lL_max = math.log10(max(valid))
        if lL_min >= lL_max:
            lL_min = lL_max - 3

        grid = [["." for _ in range(W)] for _ in range(H)]
        for p, pL in pts:
            if pL <= 0: continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W-1))
            r = H-1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H-1))
            grid[max(0,min(H-1,r))][max(0,min(W-1,c))] = "*"
        for p in np.logspace(lp_min, lp_max, W):
            pL = p
            if pL <= 0 or not (lL_min <= math.log10(pL) <= lL_max): continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W-1))
            r = H-1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H-1))
            c, r = max(0,min(W-1,c)), max(0,min(H-1,r))
            if grid[r][c] == ".": grid[r][c] = "-"

        print(f"  {bench} | Y: p_shot ∈ [{10**lL_min:.1e},{10**lL_max:.1e}]"
              f" | X: p ∈ [{10**lp_min:.1e},{10**lp_max:.1e}]")
        for row in grid:
            print("  |" + "".join(row) + "|")
        print()


def _print_summary(results):
    target_p = 1e-3
    print(f"{'='*68}")
    print(f"  Summary at p ≈ 1e-3 (lattice surgery, surface code d=3)")
    print(f"  {'Benchmark':<12} {'n_log':>6} {'n_phys':>7} {'detectors':>10}"
          f" {'obs':>4} {'p_shot':>10}")
    print(f"  {'-'*55}")
    by_bench = {}
    for row in results:
        by_bench.setdefault(row["benchmark"], []).append(row)
    for bench, rows in by_bench.items():
        closest = min(rows, key=lambda r: abs(r["p"] - target_p))
        p_str = (f"{closest['p_shot']:.4e}"
                 if closest["p_shot"] is not None else "   nan  ")
        print(f"  {bench:<12} {closest['n_logical']:>6} {closest['n_phys']:>7}"
              f" {closest['detectors']:>10} {closest['observables']:>4} {p_str:>10}")
    print()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shots",           type=int, default=5_000)
    parser.add_argument("--syndrome-rounds", type=int, default=3)
    parser.add_argument("--output",          default="stim_ls_results.csv")
    parser.add_argument("--ftqc-opt",        default=DEFAULT_FTQC_OPT)
    parser.add_argument("--dry-run",         action="store_true")
    args = parser.parse_args()

    if not os.path.exists(args.ftqc_opt):
        print(f"ERROR: ftqc-opt not found at '{args.ftqc_opt}'.")
        sys.exit(1)

    os.chdir(_SCRIPT_DIR)
    run_sweep(
        ftqc_opt=args.ftqc_opt,
        num_shots=args.shots,
        syndrome_rounds=args.syndrome_rounds,
        output_csv=args.output,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    main()
