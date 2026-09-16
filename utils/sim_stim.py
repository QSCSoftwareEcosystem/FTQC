#!/usr/bin/env python3
"""sim_stim.py — QEC simulation via STIM for the FTQC MLIR dialect.

Runs Monte Carlo QEC simulations for four benchmark circuits encoded in the
Steane [[7,1,3]] code using STIM (stabilizer simulator) and PyMatching (MWPM decoder).

Each benchmark is cast as a *round-trip* circuit:  prepare → logical gates → undo
(inverse gates) → measure.  The noiseless outcome is always |0>^n, giving
STIM deterministic observables (required for detector error models).

Usage:
    qasm.venv/bin/python sim_stim.py [--shots N] [--output results.csv]

Physical noise model (depolarising + measurement errors):
    DEPOLARIZE1(p) after single-qubit Clifford gates
    DEPOLARIZE2(p) after two-qubit CX/CZ gates
    X_ERROR(p)     after RESET and before M

Error rate sweep:  p ∈ [1e-4, 1.5e-2]  (18 log-uniform points)
Threshold for Steane d=3 (depolarising):  p_th ≈ 1%
"""

import sys, os, csv, math, argparse, time
import stim
import pymatching
import numpy as np

# ---------------------------------------------------------------------------
# Steane [[7,1,3]] code constants
# ---------------------------------------------------------------------------

STEANE_DATA  = 7
STEANE_XANC  = 3
STEANE_ZANC  = 3
STEANE_TOTAL = STEANE_DATA + STEANE_XANC + STEANE_ZANC  # 13

STEANE_STABS = [
    [0, 2, 4, 6],
    [1, 2, 5, 6],
    [3, 4, 5, 6],
]
ENCODE_H_QUBITS = [0, 1, 3]

P_THRESHOLD = 0.01  # Steane code approximate threshold (~1%)

# ---------------------------------------------------------------------------
# STIM circuit builder for Steane [[7,1,3]] code
# ---------------------------------------------------------------------------

class SteaneCircuit:
    """Builds STIM circuits for Steane-encoded logical circuits.

    Physical qubit layout per logical qubit (base index B):
        data  : B+0 .. B+6
        X-anc : B+7  (Sx1), B+8  (Sx2), B+9  (Sx3)
        Z-anc : B+10 (Sz1), B+11 (Sz2), B+12 (Sz3)
    """

    def __init__(self, n_logical: int, p: float, syndrome_rounds: int = 3):
        self.n_logical      = n_logical
        self.p              = p
        self.syndrome_rounds = syndrome_rounds
        self.blocks         = [i * STEANE_TOTAL for i in range(n_logical)]
        self._circ          = stim.Circuit()
        self._total_meas    = 0
        # Per-block: absolute measurement index of the *start* of the last round.
        self._prev_round_start = [-1] * n_logical

    # -----------------------------------------------------------------------
    # Low-level emission helpers
    # -----------------------------------------------------------------------

    def _R(self, qubits):
        self._circ.append("R", qubits)
        if self.p > 0:
            self._circ.append("X_ERROR", qubits, self.p)

    def _1Q(self, gate, qubits):
        self._circ.append(gate, qubits)
        if self.p > 0:
            self._circ.append("DEPOLARIZE1", qubits, self.p)

    def _2Q(self, gate, pairs):
        self._circ.append(gate, pairs)
        if self.p > 0:
            self._circ.append("DEPOLARIZE2", pairs, self.p)

    def _M(self, qubits):
        if self.p > 0:
            self._circ.append("X_ERROR", qubits, self.p)
        self._circ.append("M", qubits)
        self._total_meas += len(qubits)

    def _data(self, b, i):  return self.blocks[b] + i
    def _xanc(self, b, i):  return self.blocks[b] + STEANE_DATA + i
    def _zanc(self, b, i):  return self.blocks[b] + STEANE_DATA + STEANE_XANC + i
    def _all_data(self, b): return list(range(self.blocks[b], self.blocks[b] + STEANE_DATA))

    # -----------------------------------------------------------------------
    # Encoding
    # -----------------------------------------------------------------------

    def encode_zero(self, b: int):
        """Emit the |0_L> encoding circuit for block b."""
        base = self.blocks[b]
        all7 = list(range(base, base + STEANE_DATA))
        self._R(all7)
        h_qubits = [base + off for off in ENCODE_H_QUBITS]
        self._1Q("H", h_qubits)
        for stab in STEANE_STABS:
            ctrl = base + stab[0]
            for j in range(1, len(stab)):
                self._2Q("CX", [ctrl, base + stab[j]])

    def encode_plus(self, b: int):
        self.encode_zero(b)
        self._1Q("H", self._all_data(b))

    # -----------------------------------------------------------------------
    # Syndrome extraction  (one round = 6 measurements)
    # -----------------------------------------------------------------------

    def syndrome_round(self, b: int):
        """One syndrome extraction round; emits DETECTOR records."""
        round_start = self._total_meas

        # X syndrome
        x_ancs = [self._xanc(b, i) for i in range(STEANE_XANC)]
        self._R(x_ancs)
        self._1Q("H", x_ancs)
        for s, stab in enumerate(STEANE_STABS):
            ctrl = self._xanc(b, s)
            pairs = []
            for off in stab:
                pairs += [ctrl, self._data(b, off)]
            self._2Q("CX", pairs)
        self._1Q("H", x_ancs)
        self._M(x_ancs)

        # Z syndrome
        z_ancs = [self._zanc(b, i) for i in range(STEANE_ZANC)]
        self._R(z_ancs)
        for s, stab in enumerate(STEANE_STABS):
            tgt = self._zanc(b, s)
            pairs = []
            for off in stab:
                pairs += [self._data(b, off), tgt]
            self._2Q("CX", pairs)
        self._M(z_ancs)

        # 6 syndrome bits (0..5): bit i was at absolute measurement (round_start + i).
        # Its offset from current end = self._total_meas - (round_start + i).
        cur_end = self._total_meas  # = round_start + 6
        prev    = self._prev_round_start[b]

        for i in range(6):
            cur_off  = cur_end - (round_start + i)           # offset for this round's bit i
            targets  = [stim.target_rec(-cur_off)]
            if prev >= 0:
                prev_off = cur_end - (prev + i)              # offset for previous round's bit i
                targets.append(stim.target_rec(-prev_off))
            self._circ.append("DETECTOR", targets)

        self._prev_round_start[b] = round_start

    def syndrome(self, b: int):
        for _ in range(self.syndrome_rounds):
            self.syndrome_round(b)

    # -----------------------------------------------------------------------
    # Logical gates (transversal)
    # -----------------------------------------------------------------------

    def h(self, b): self._1Q("H", self._all_data(b))
    def x(self, b): self._1Q("X", self._all_data(b))
    def z(self, b): self._1Q("Z", self._all_data(b))
    def s(self, b): self._1Q("S", self._all_data(b))

    def cnot(self, ctrl: int, tgt: int):
        pairs = []
        for i in range(STEANE_DATA):
            pairs += [self._data(ctrl, i), self._data(tgt, i)]
        self._2Q("CX", pairs)

    def cz(self, q0: int, q1: int):
        pairs = []
        for i in range(STEANE_DATA):
            pairs += [self._data(q0, i), self._data(q1, i)]
        self._2Q("CZ", pairs)

    def measure_all(self, obs_id: int, *blocks: int):
        """Measure all blocks and define ONE observable as XOR of all Z measurements.

        In the noiseless case the XOR of all logical Z measurements should be 0.
        """
        start_meas = self._total_meas
        for b in blocks:
            self._M(self._all_data(b))
        # Build OBSERVABLE_INCLUDE targeting all measurements from start_meas onward.
        n_meas = self._total_meas - start_meas   # = STEANE_DATA * len(blocks)
        targets = [stim.target_rec(-n_meas + i) for i in range(n_meas)]
        self._circ.append("OBSERVABLE_INCLUDE", targets, obs_id)

    def measure_z(self, b: int, obs_id: int):
        """Measure block b in Z basis; logical Z = XOR of all 7 data measurements."""
        self._M(self._all_data(b))
        targets = [stim.target_rec(-STEANE_DATA + i) for i in range(STEANE_DATA)]
        self._circ.append("OBSERVABLE_INCLUDE", targets, obs_id)

    def build(self) -> stim.Circuit:
        return self._circ


# ---------------------------------------------------------------------------
# Benchmark circuit builders (round-trip: prepare → undo → measure → 0)
# ---------------------------------------------------------------------------

def build_bell(n: int, p: float) -> stim.Circuit:
    """
    Bell-state round-trip: H⊗CNOT then CNOT⊗H (inverse) → all qubits back to |0>.
    This verifies the full logical-CNOT + logical-H cycle under noise.
    Each pair (q0, q1) acts independently.
    """
    assert n >= 2 and n % 2 == 0
    sc = SteaneCircuit(n, p)
    for i in range(n):
        sc.encode_zero(i)
        sc.syndrome(i)

    for pair in range(n // 2):
        q0, q1 = 2 * pair, 2 * pair + 1
        # Forward: H q0, CNOT q0→q1
        sc.h(q0);    sc.syndrome(q0)
        sc.cnot(q0, q1); sc.syndrome(q0); sc.syndrome(q1)
        # Inverse: CNOT q0→q1, H q0  (undo Bell preparation)
        sc.cnot(q0, q1); sc.syndrome(q0); sc.syndrome(q1)
        sc.h(q0);    sc.syndrome(q0)
        # Measure: both qubits should be Z=0 (back to |00>)
        sc.measure_z(q0, 2 * pair)
        sc.measure_z(q1, 2 * pair + 1)

    return sc.build()


def build_ghz(n: int, p: float) -> stim.Circuit:
    """
    GHZ round-trip: H q0, CNOT q0→q1, ..., CNOT q0→q_{n-1},
    then reverse (CNOT q0→q_{n-1}, ..., CNOT q0→q1, H q0).
    All qubits return to |0>.
    """
    assert n >= 2
    sc = SteaneCircuit(n, p)
    for i in range(n):
        sc.encode_zero(i)
        sc.syndrome(i)

    sc.h(0); sc.syndrome(0)
    for i in range(1, n):
        sc.cnot(0, i); sc.syndrome(0); sc.syndrome(i)

    # Inverse
    for i in reversed(range(1, n)):
        sc.cnot(0, i); sc.syndrome(0); sc.syndrome(i)
    sc.h(0); sc.syndrome(0)

    for i in range(n):
        sc.measure_z(i, i)

    return sc.build()


def build_grover(n: int, p: float) -> stim.Circuit:
    """
    Grover diffusion round-trip: one full diffusion operator D = H^n X^n CZ-chain X^n H^n,
    followed by its inverse (same circuit, self-inverse), returning all qubits to |0>.
    """
    assert n >= 2
    sc = SteaneCircuit(n, p)
    for i in range(n):
        sc.encode_zero(i)
        sc.syndrome(i)

    def diffusion_layer():
        for i in range(n): sc.h(i); sc.syndrome(i)
        for i in range(n): sc.x(i); sc.syndrome(i)
        for i in range(n - 1): sc.cz(i, i + 1); sc.syndrome(i); sc.syndrome(i + 1)
        for i in range(n): sc.x(i); sc.syndrome(i)
        for i in range(n): sc.h(i); sc.syndrome(i)

    # Apply and undo the diffusion operator (it is self-inverse since D^2 = I for Clifford approx)
    diffusion_layer()  # D
    diffusion_layer()  # D^{-1} = D  (self-inverse for this Clifford circuit)

    for i in range(n):
        sc.measure_z(i, i)

    return sc.build()


def build_qft(n: int, p: float) -> stim.Circuit:
    """
    QFT round-trip: Clifford-only QFT (using CZ for controlled-phase) followed by
    inverse QFT (IQFT), returning all qubits to |0>.
    """
    assert n >= 2
    sc = SteaneCircuit(n, p)
    for i in range(n):
        sc.encode_zero(i)
        sc.syndrome(i)

    def qft_forward():
        for i in range(n):
            sc.h(i); sc.syndrome(i)
            for j in range(i + 1, n):
                sc.cz(i, j); sc.syndrome(i); sc.syndrome(j)
        # Bit-reversal SWAP (3 CNOT each)
        for i in range(n // 2):
            j = n - 1 - i
            if i != j:
                sc.cnot(i, j); sc.cnot(j, i); sc.cnot(i, j)
                sc.syndrome(i); sc.syndrome(j)

    def qft_inverse():
        # IQFT = QFT† = reverse order + conjugate gates
        # Bit-reversal SWAP (3 CNOT, self-inverse)
        for i in range(n // 2):
            j = n - 1 - i
            if i != j:
                sc.cnot(i, j); sc.cnot(j, i); sc.cnot(i, j)
                sc.syndrome(i); sc.syndrome(j)
        # Inverse QFT layers
        for i in reversed(range(n)):
            for j in reversed(range(i + 1, n)):
                # CZ is self-inverse
                sc.cz(i, j); sc.syndrome(i); sc.syndrome(j)
            sc.h(i); sc.syndrome(i)

    qft_forward()
    qft_inverse()

    for i in range(n):
        sc.measure_z(i, i)

    return sc.build()


class _Rec:
    """Records transversal logical gates on a SteaneCircuit so a round-trip
    inverse can be replayed automatically (guaranteeing the noiseless outcome
    is |0...0>).  T is represented by the Clifford stand-in S, matching the
    --ftqc-lower-to-stim back end; S-dagger is S.Z."""
    def __init__(self, sc): self.sc = sc; self.ops = []
    def _syn(self, *bs):
        for b in bs: self.sc.syndrome(b)
    # Only graph-decodable Clifford gates (H, X, Z, CNOT, CZ) are used so the
    # detector error model decomposes for MWPM; transversal S would couple X/Z
    # errors into non-graphlike faults.  Non-Clifford content is thus abstracted
    # to a Clifford stand-in, exactly as build_qft does (CZ for controlled-phase).
    def h(self, b):   self.sc.h(b); self._syn(b); self.ops.append(('h', b))
    def x(self, b):   self.sc.x(b); self._syn(b); self.ops.append(('x', b))
    def z(self, b):   self.sc.z(b); self._syn(b); self.ops.append(('z', b))
    def cnot(self, a, b): self.sc.cnot(a, b); self._syn(a, b); self.ops.append(('cnot', a, b))
    def cz(self, a, b):   self.sc.cz(a, b); self._syn(a, b); self.ops.append(('cz', a, b))
    def inverse(self):
        # every gate used here is its own inverse
        for op in reversed(self.ops):
            m, args = op[0], op[1:]
            if   m == 'h':    self.sc.h(*args); self._syn(*args)
            elif m == 'x':    self.sc.x(*args); self._syn(*args)
            elif m == 'z':    self.sc.z(*args); self._syn(*args)
            elif m == 'cnot': self.sc.cnot(*args); self._syn(*args)
            elif m == 'cz':   self.sc.cz(*args); self._syn(*args)


def build_qaoa(n: int, p: float) -> stim.Circuit:
    """One ring-QAOA layer as a Clifford stand-in: the cost coupling on each ring
    edge is a CZ (the Clifford Rzz(pi/2)) and the mixer an H per qubit, followed
    by the inverse.  As with QFT-3 the non-Clifford angle is abstracted so the
    circuit is MWPM-decodable; the structure (ring, depth) is preserved."""
    sc = SteaneCircuit(n, p)
    for i in range(n): sc.encode_zero(i); sc.syndrome(i)
    g = _Rec(sc)
    for i in range(n): g.cz(i, (i + 1) % n)  # cost layer on ring edges
    for i in range(n): g.h(i)                # mixer
    g.inverse()
    for i in range(n): sc.measure_z(i, i)
    return sc.build()


def build_vqe(n: int, p: float) -> stim.Circuit:
    """Hardware-efficient VQE ansatz layer as a Clifford stand-in: a Z rotation
    per qubit around a linear CNOT ladder, twice, followed by the inverse."""
    sc = SteaneCircuit(n, p)
    for i in range(n): sc.encode_zero(i); sc.syndrome(i)
    g = _Rec(sc)
    for i in range(n): g.z(i)                # rotation sublayer 1
    for i in range(n - 1): g.cnot(i, i + 1)  # CNOT ladder
    for i in range(n): g.z(i)                # rotation sublayer 2
    g.inverse()
    for i in range(n): sc.measure_z(i, i)
    return sc.build()


def build_grover_mcx(n: int, p: float) -> stim.Circuit:
    """Grover iteration whose oracle and diffusion each apply a multi-controlled
    Z, here the Clifford stand-in of a CZ chain across all n qubits (denser than
    Grover-2's single CZ, reflecting the multi-control cost), followed by the
    inverse.  A nearest-neighbour chain is used rather than a star from qubit 0:
    the star concentrates every CZ on one qubit, whose high-degree X/Z error
    correlations make the detector error model non-graphlike, so MWPM cannot
    decode it (and pymatching stalls on the resulting instances); the chain keeps
    each qubit's degree at two, matching QAOA's ring, so the DEM stays graphlike
    and decodable.  As with QFT-3 the non-Clifford content is abstracted."""
    sc = SteaneCircuit(n, p)
    for i in range(n): sc.encode_zero(i); sc.syndrome(i)
    g = _Rec(sc)
    def mcz():
        for i in range(n - 1): g.cz(i, i + 1)   # CZ chain = multi-control stand-in
    for i in range(n): g.h(i)                # superposition
    mcz()                                    # oracle
    for i in range(n): g.h(i)                # diffusion
    for i in range(n): g.x(i)
    mcz()
    for i in range(n): g.x(i)
    for i in range(n): g.h(i)
    g.inverse()
    for i in range(n): sc.measure_z(i, i)
    return sc.build()


# ---------------------------------------------------------------------------
# Simulation + MWPM decoding
# ---------------------------------------------------------------------------

def simulate(circuit: stim.Circuit, num_shots: int) -> float:
    """
    Run Monte Carlo shots, decode with MWPM, return logical error rate for obs 0.
    """
    # Try to build a detector error model; fall back gracefully on failure.
    dem = None
    for kwargs in [
        dict(decompose_errors=True,  approximate_disjoint_errors=True),
        dict(approximate_disjoint_errors=True),
        dict(),
    ]:
        try:
            dem = circuit.detector_error_model(**kwargs)
            break
        except Exception:
            pass

    if dem is None or len(dem) == 0:
        # Fallback: sample raw observables (no decoding; measures bare logical error).
        sampler = circuit.compile_sampler()
        n_meas  = circuit.num_measurements
        n_obs   = circuit.num_observables
        if n_obs == 0:
            return 0.0
        samples   = sampler.sample(num_shots)
        # Last n_obs bits are OBSERVABLE_INCLUDE results? No — use compile_detector_sampler.
        det_sampler = circuit.compile_detector_sampler()
        det_samples = det_sampler.sample(num_shots, append_observables=True)
        n_det = circuit.num_detectors
        obs   = det_samples[:, n_det:]
        return float(np.mean(obs[:, 0])) if obs.shape[1] > 0 else 0.0

    matcher = pymatching.Matching.from_detector_error_model(dem)
    sampler  = circuit.compile_detector_sampler()
    n_det    = circuit.num_detectors
    samples  = sampler.sample(num_shots, append_observables=True)
    det_evts = samples[:, :n_det]
    obs_flip = samples[:, n_det:]

    if obs_flip.shape[1] == 0:
        return 0.0

    preds   = matcher.decode_batch(det_evts)
    n_errs  = int(np.sum(preds[:, 0] != obs_flip[:, 0]))
    return n_errs / num_shots


# ---------------------------------------------------------------------------
# MLIR pass output validation
# ---------------------------------------------------------------------------

FTQC_OPT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build/src/ftqc-opt")
TEST_MLIR = {
    "Bell":   "test/Integration/bell-state-ft.mlir",
    "GHZ":    "test/Integration/ghz4-steane.mlir",
    "Grover": "test/Integration/grover-ft.mlir",
    "QFT":    "test/Integration/qft3-steane.mlir",
}

def validate_mlir_pass():
    """Verify --ftqc-lower-to-stim produces parseable STIM circuits."""
    if not os.path.exists(FTQC_OPT):
        print("  ftqc-opt not found; skipping MLIR pass validation.\n")
        return

    print(f"{'='*64}")
    print(f"  Validating --ftqc-lower-to-stim MLIR pass output")
    print(f"{'='*64}\n")

    for name, mlir_file in TEST_MLIR.items():
        if not os.path.exists(mlir_file):
            print(f"  {name:<10}: MLIR file not found, skipping.")
            continue
        try:
            r = __import__('subprocess').run(
                [FTQC_OPT,
                 '--ftqc-lower-to-stim=physical-error-rate=0.001 syndrome-rounds=1 include-comments=false',
                 mlir_file],
                capture_output=True, text=True, timeout=15
            )
            stim_str = r.stderr
            print("------------------- STIM circuit:\n", stim_str)
            circuit  = stim.Circuit(stim_str)
            print(f"  {name:<10}: OK  —  {circuit.num_qubits} qubits, "
                  f"{circuit.num_detectors} detectors, "
                  f"{circuit.num_observables} observables")
        except Exception as e:
            print(f"  {name:<10}: FAIL  — {e}")
    print()


# ---------------------------------------------------------------------------
# Main sweep
# ---------------------------------------------------------------------------

BENCHMARKS = {
    "Bell":   build_bell,
    "GHZ":    build_ghz,
    "Grover": build_grover,
    "QFT":    build_qft,
}

BENCH_N = {
    "Bell":   4,   # 4 logical qubits = 2 Bell pairs
    "GHZ":    3,
    "Grover": 2,
    "QFT":    3,
}

# 18 log-uniform points from 1e-4 to 1.5e-2
ERROR_RATES = np.logspace(-4, -1.82, 18)


def run_sweep(num_shots: int, output_csv: str):
    print(f"\n{'='*64}")
    print(f"  FTQC STIM QEC Simulation — Steane [[7,1,3]]")
    print(f"  Shots per point : {num_shots:,}")
    print(f"  Syndrome rounds : 3 per logical gate")
    print(f"  Threshold (d=3) : p_th ≈ {P_THRESHOLD:.0%}")
    print(f"{'='*64}\n")

    all_results = []

    for bench_name, build_fn in BENCHMARKS.items():
        n = BENCH_N[bench_name]
        print(f"  Benchmark: {bench_name}  (n={n} logical qubits, "
              f"{n * STEANE_TOTAL} physical qubits)")
        print(f"  {'p':>10}  {'p_L':>10}  {'p_L/p':>8}  {'err shots':>10}")
        print(f"  {'-'*46}")

        for p in ERROR_RATES:
            t0 = time.time()
            try:
                circuit = build_fn(n, p)
                p_L = simulate(circuit, num_shots)
            except Exception as e:
                print(f"  {p:>10.2e}  {'ERROR':>10}  {str(e)[:40]}")
                continue
            elapsed = time.time() - t0
            ratio   = p_L / p if p > 0 else float('nan')
            n_err   = round(p_L * num_shots)
            print(f"  {p:>10.2e}  {p_L:>10.4e}  {ratio:>8.2f}  {n_err:>10d}"
                  f"   ({elapsed:.1f}s)")
            all_results.append({
                "benchmark": bench_name, "n_logical": n, "p": float(p),
                "p_L": float(p_L), "n_phys": n * STEANE_TOTAL,
            })

        print()

    # Save CSV
    with open(output_csv, "w", newline="") as f:
        writer = csv.DictWriter(f,
            fieldnames=["benchmark","n_logical","p","p_L","n_phys"])
        writer.writeheader()
        writer.writerows(all_results)
    print(f"  Results saved to: {output_csv}\n")

    print_ascii_plots(all_results)
    validate_mlir_pass()


def print_ascii_plots(results):
    W, H = 55, 14
    print(f"{'='*64}")
    print(f"  ASCII plots: p_L (Y) vs p (X), log-log scale")
    print(f"  '*' = simulated,  '-' = threshold line (p_L = p)")
    print(f"{'='*64}\n")

    by_bench = {}
    for row in results:
        by_bench.setdefault(row["benchmark"], []).append((row["p"], row["p_L"]))

    for bench, pts in by_bench.items():
        ps, pLs = zip(*pts) if pts else ([], [])
        if not ps:
            continue
        lp_min = math.log10(min(ps))
        lp_max = math.log10(max(ps))
        valid  = [v for v in pLs if v > 0]
        if not valid:
            continue
        lL_min = math.log10(min(valid))
        lL_max = math.log10(max(valid))
        if lL_min >= lL_max:
            lL_min = lL_max - 3

        grid = [['.' for _ in range(W)] for _ in range(H)]

        for p, pL in pts:
            if pL <= 0: continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W - 1))
            r = H - 1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H - 1))
            c = max(0, min(W-1, c)); r = max(0, min(H-1, r))
            grid[r][c] = '*'

        for p in np.logspace(lp_min, lp_max, W):
            pL = p
            if pL <= 0 or not (lL_min <= math.log10(pL) <= lL_max): continue
            c = round((math.log10(p)  - lp_min) / (lp_max - lp_min) * (W - 1))
            r = H - 1 - round((math.log10(pL) - lL_min) / (lL_max - lL_min) * (H - 1))
            c = max(0, min(W-1, c)); r = max(0, min(H-1, r))
            if grid[r][c] == '.': grid[r][c] = '-'

        print(f"  {bench} (n={BENCH_N[bench]}) | Y: p_L ∈ [{10**lL_min:.1e},{10**lL_max:.1e}] | X: p ∈ [{10**lp_min:.1e},{10**lp_max:.1e}]")
        for row in grid:
            print("  |" + "".join(row) + "|")
        print()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--shots",  type=int, default=10_000)
    parser.add_argument("--output", default="stim_results.csv")
    args = parser.parse_args()
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    run_sweep(args.shots, args.output)


if __name__ == "__main__":
    main()
