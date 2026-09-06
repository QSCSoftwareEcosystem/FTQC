#!/usr/bin/env python3
"""sim_stim_memory.py -- FTQC+Stim+MWPM decoded memory experiment.

Reproduces Figure fig:mem_decoded: the compiler emits the Steane [[7,1,3]] Stim
circuit for the memory kernel (init |0_L>, N syndrome rounds, measure) directly
via `ftqc-opt --ftqc-lower-to-stim`, and we decode it with PyMatching MWPM,
sweeping the physical depolarizing error rate p.  Reports the decoded logical
error rate p_L, the undecoded rate, and the estimator's analytic p_total.

Requires stim + pymatching + numpy (e.g. a venv:
    python3 -m venv qec.venv && qec.venv/bin/pip install stim pymatching numpy).
Usage:
    qec.venv/bin/python sim_stim_memory.py [--shots N] [--rounds R] [--opt PATH]
"""
import argparse, subprocess, tempfile, os
import numpy as np
import stim, pymatching

MEM_MLIR = """\
func.func @mem() -> i1 {
  %q = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %o, %s = ftqc.extract_syndrome %q
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %b = ftqc.logical_measure %o
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  return %b : i1
}
"""


def stim_circuit(opt, mlir_path, p, rounds):
    """Lower the memory kernel to a Stim circuit at physical error rate p."""
    r = subprocess.run(
        [opt, mlir_path,
         f"--ftqc-lower-to-stim=physical-error-rate={p} syndrome-rounds={rounds}"],
        capture_output=True, text=True)
    # The Stim text is emitted on stderr; strip comment lines.
    lines = [l for l in r.stderr.splitlines()
             if l.strip() and not l.strip().startswith("#")]
    return stim.Circuit("\n".join(lines))


def main():
    ap = argparse.ArgumentParser()
    _root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # utils/..
    ap.add_argument("--opt", default=os.path.join(_root, "build/src/ftqc-opt"))
    ap.add_argument("--shots", type=int, default=60000)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--points", type=int, default=12)
    args = ap.parse_args()

    with tempfile.NamedTemporaryFile("w", suffix=".mlir", delete=False) as f:
        f.write(MEM_MLIR)
        mlir_path = f.name

    ps = np.geomspace(1e-3, 1.5e-1, args.points)
    p_th = 0.01  # Steane threshold used by the resource estimator
    print(f"{'p':>10} {'p_L_decoded':>12} {'p_L_raw':>10} {'p_total':>10}")
    try:
        for p in ps:
            c = stim_circuit(args.opt, mlir_path, p, args.rounds)
            dem = c.detector_error_model(decompose_errors=True)
            matcher = pymatching.Matching.from_detector_error_model(dem)
            det, obs = c.compile_detector_sampler().sample(
                args.shots, separate_observables=True)
            pred = matcher.decode_batch(det)
            p_l = float(np.mean(pred[:, 0] != obs[:, 0]))
            p_raw = float(np.mean(obs[:, 0]))
            p_total = min(1.0, 0.1 * (p / p_th) ** 2)  # leading-order per round
            print(f"{p:10.4e} {p_l:12.4f} {p_raw:10.4f} {p_total:10.4e}")
    finally:
        os.unlink(mlir_path)


if __name__ == "__main__":
    main()
