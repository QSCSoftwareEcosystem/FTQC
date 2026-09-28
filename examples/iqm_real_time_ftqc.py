#!/usr/bin/env python3
"""Real-time fault-tolerant quantum computing on IQM Emerald (54 qubits).

Three logical qubits encoded in the Steane [[7,1,3]] code, driven through a
logical circuit of single- and two-qubit gates, with a PyMatching-based decoder
in the feedback loop:

    build segment -> submit -> measure syndromes -> PyMatching decode
        -> update Pauli frame -> fold correction into the next segment -> repeat

Layout (39 of 54 physical qubits):

    block b in {0,1,2}:  7 data + 3 X-stabiliser ancillas + 3 Z-stabiliser ancillas

Stabilisers (self-dual CSS, Hx == Hz), qubit indices within a block:

    S0 = {0,2,4,6}   S1 = {1,2,5,6}   S2 = {3,4,5,6}

Decoder.  PyMatching cannot be handed the Steane check matrix directly: qubit 6
lies in all three stabilisers, so that column has weight 3 and
``Matching.from_check_matrix`` rejects it (a matching graph needs at most two
ones per column).  Steane is the smallest 2D triangular colour code, so this
module uses the standard *restriction decoder*: three PyMatching graphs, one per
pair of stabiliser faces, whose supports are lifted to a single correction and
checked against the full syndrome.  ``--self-test`` verifies exhaustively that
every syndrome decodes consistently and every single-qubit error is recovered.

Backends: ``--backend stabilizer`` (default) is Aer's Clifford simulator with a
depolarising noise model, which is what makes 39 qubits tractable offline --
tune it with ``--p1``, ``--p2``, ``--p-meas``.  ``--backend emerald`` (or any
IQM alias) submits to real hardware.  ``--backend aphrodite`` is IQM's fake
54-qubit device, but it is a full statevector model and cannot run a circuit
this wide.

Two execution modes:

``--mode loop`` (default)
    Host-in-the-loop.  Each round is its own job: the circuit re-encodes, replays
    the earlier segments with their already-decoded corrections applied, runs the
    new segment, and extracts one syndrome round.  The host decodes with
    PyMatching and folds the result into the next submission.  This is what runs
    on Emerald today.

``--mode dynamic``
    True in-circuit feedback via Qiskit ``if_test``, with the decoder compiled to
    a lookup table (what a real-time control stack would do for a distance-3
    code; PyMatching generates the table offline).  Requires a backend whose
    target advertises ``if_else``.  IQM's current target does not, so this mode
    reports that and stops unless ``--allow-fallback`` is given.

Honest caveat on ``loop`` mode: quantum state does not survive between separate
job submissions, so each round re-prepares the encoded state rather than
continuing a single uninterrupted logical computation.  The feedback protocol,
the decoder, and the Pauli-frame bookkeeping are real; the state persistence is
not.  Only ``--mode dynamic`` keeps the state alive across a feedback event, and
that needs hardware support for conditional branching.

Syndrome extraction here is unflagged (one bare ancilla per stabiliser), so a
single ancilla fault can propagate into a weight-2 data error.  That is below
the bar for genuine fault tolerance at d=3; flag qubits would be the next step.

Examples
--------
    # verify the decoder exhaustively
    realqpu/bin/python iqm_real_time_ftqc.py --self-test

    # offline feedback loop, default program h0 ; cx01 ; cz12 ; x2
    realqpu/bin/python iqm_real_time_ftqc.py --shots 512

    # show the feed-forward firing: static faults that the loop then cancels
    realqpu/bin/python iqm_real_time_ftqc.py --p1 0 --p2 0 --p-meas 0 \
        --inject 0:3:X --inject 2:5:Z

    # in-circuit conditional feedback instead of host-in-the-loop
    realqpu/bin/python iqm_real_time_ftqc.py --mode dynamic --shots 256

    # real hardware
    IQM_TOKEN=... realqpu/bin/python iqm_real_time_ftqc.py \
        --backend emerald --shots 1024 --results-json rt.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter
from dataclasses import dataclass
from itertools import combinations
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np

try:
    import pymatching
except ImportError:  # pragma: no cover
    sys.exit("pymatching is required: pip install pymatching")

from qiskit import ClassicalRegister, QuantumCircuit, QuantumRegister

# --------------------------------------------------------------------------
# Steane [[7,1,3]] code
# --------------------------------------------------------------------------

STEANE_STAB: list[list[int]] = [[0, 2, 4, 6], [1, 2, 5, 6], [3, 4, 5, 6]]
N_DATA = 7
N_STAB = 3
N_LOGICAL = 3


def steane_check_matrix() -> np.ndarray:
    """3x7 binary parity-check matrix. Hx == Hz because Steane is self-dual."""
    h = np.zeros((N_STAB, N_DATA), dtype=np.uint8)
    for row, support in enumerate(STEANE_STAB):
        for q in support:
            h[row, q] = 1
    return h


def syndrome_of(error: np.ndarray, h: np.ndarray | None = None) -> np.ndarray:
    h = steane_check_matrix() if h is None else h
    return (h @ (error % 2)) % 2


# --------------------------------------------------------------------------
# Decoder
# --------------------------------------------------------------------------


class SteaneRestrictionDecoder:
    """PyMatching restriction decoder for the Steane / 2D colour code.

    The three stabiliser faces are three colours.  Restricting to each pair of
    colours gives a 2x7 check matrix whose columns have weight <= 2, which is a
    valid PyMatching graph.  Decoding all three restrictions gives a candidate
    support; the lift picks the minimum-weight subset of that support whose
    syndrome matches the full three-bit syndrome.

    One syndrome (all three faces lit, i.e. an error on qubit 6) is not produced
    by the lift, because qubit 6 is the weight-3 column that PyMatching cannot
    represent.  For that case the decoder falls back to an exact minimum-weight
    search over the full code, which at d=3 is seven candidates.
    """

    PAIRS: tuple[tuple[int, int], ...] = ((0, 1), (0, 2), (1, 2))

    def __init__(self) -> None:
        self.h = steane_check_matrix()
        self.matchings = [
            pymatching.Matching.from_check_matrix(self.h[list(pair)])
            for pair in self.PAIRS
        ]
        self.stats = Counter()

    def _lift_candidates(self, syndrome: np.ndarray) -> list[int]:
        support: set[int] = set()
        for pair, matching in zip(self.PAIRS, self.matchings):
            restricted = np.array([syndrome[i] for i in pair], dtype=np.uint8)
            correction = np.asarray(matching.decode(restricted), dtype=np.uint8)
            support.update(int(q) for q in np.nonzero(correction)[0])
        return sorted(support)

    def _search(self, syndrome: np.ndarray, pool: Sequence[int]) -> np.ndarray | None:
        for weight in range(len(pool) + 1):
            for combo in combinations(pool, weight):
                err = np.zeros(N_DATA, dtype=np.uint8)
                if combo:
                    err[list(combo)] = 1
                if np.array_equal(syndrome_of(err, self.h), syndrome):
                    return err
        return None

    def decode(self, syndrome: Iterable[int]) -> np.ndarray:
        """Map a 3-bit syndrome to a 7-bit Pauli correction on the data qubits."""
        syn = np.asarray(list(syndrome), dtype=np.uint8) % 2
        correction = self._search(syn, self._lift_candidates(syn))
        if correction is not None:
            self.stats["restriction"] += 1
            return correction
        correction = self._search(syn, range(N_DATA))
        if correction is None:  # unreachable for a 3-bit syndrome
            self.stats["undecodable"] += 1
            return np.zeros(N_DATA, dtype=np.uint8)
        self.stats["exact_fallback"] += 1
        return correction

    def lookup_table(self) -> dict[int, np.ndarray]:
        """All 2^3 syndromes precomputed, for compiling into a real-time path."""
        table = {}
        for value in range(1 << N_STAB):
            syn = [(value >> i) & 1 for i in range(N_STAB)]
            table[value] = self.decode(syn)
        return table

    def self_test(self, verbose: bool = True) -> bool:
        ok = True
        for value in range(1 << N_STAB):
            syn = np.array([(value >> i) & 1 for i in range(N_STAB)], dtype=np.uint8)
            err = self.decode(syn)
            consistent = np.array_equal(syndrome_of(err, self.h), syn)
            ok &= consistent
            if verbose:
                print(
                    f"  syndrome {list(int(b) for b in syn)} -> correction "
                    f"{list(int(b) for b in err)} (weight {int(err.sum())})"
                    f" {'ok' if consistent else 'INCONSISTENT'}"
                )
        for q in range(N_DATA):
            err = np.zeros(N_DATA, dtype=np.uint8)
            err[q] = 1
            recovered = self.decode(syndrome_of(err, self.h))
            exact = np.array_equal(recovered, err)
            ok &= exact
            if verbose:
                print(
                    f"  single X on data q{q} -> decoded "
                    f"{list(int(b) for b in recovered)} {'ok' if exact else 'MISMATCH'}"
                )
        return bool(ok)


# --------------------------------------------------------------------------
# Physical layout
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class BlockLayout:
    """Physical qubit indices for one Steane block."""

    data: tuple[int, ...]
    anc_x: tuple[int, ...]  # measure X-stabilisers, detect Z errors
    anc_z: tuple[int, ...]  # measure Z-stabilisers, detect X errors

    @property
    def all_qubits(self) -> tuple[int, ...]:
        return self.data + self.anc_x + self.anc_z


def build_layout(n_logical: int = N_LOGICAL, offset: int = 0) -> list[BlockLayout]:
    """Contiguous allocation: 13 physical qubits per logical qubit."""
    blocks = []
    stride = N_DATA + 2 * N_STAB
    for b in range(n_logical):
        base = offset + b * stride
        blocks.append(
            BlockLayout(
                data=tuple(range(base, base + N_DATA)),
                anc_x=tuple(range(base + N_DATA, base + N_DATA + N_STAB)),
                anc_z=tuple(range(base + N_DATA + N_STAB, base + stride)),
            )
        )
    return blocks


# --------------------------------------------------------------------------
# Logical circuit primitives
# --------------------------------------------------------------------------


def encode_zero(qc: QuantumCircuit, q: QuantumRegister, blk: BlockLayout) -> None:
    """Prepare |0_L>: H on the three seed qubits, then the 9 CNOTs."""
    seeds = (0, 1, 3)
    for s in seeds:
        qc.h(q[blk.data[s]])
    for seed, support in zip(seeds, STEANE_STAB):
        for target in support:
            if target != seed:
                qc.cx(q[blk.data[seed]], q[blk.data[target]])


def logical_x(qc, q, blk) -> None:
    for d in blk.data:
        qc.x(q[d])


def logical_z(qc, q, blk) -> None:
    for d in blk.data:
        qc.z(q[d])


def logical_h(qc, q, blk) -> None:
    for d in blk.data:
        qc.h(q[d])


def logical_cnot(qc, q, ctrl: BlockLayout, tgt: BlockLayout) -> None:
    """Transversal CNOT: valid because Steane is a self-dual CSS code."""
    for c, t in zip(ctrl.data, tgt.data):
        qc.cx(q[c], q[t])


def logical_cz(qc, q, a: BlockLayout, b: BlockLayout) -> None:
    """CZ_L built from H_L and CNOT_L so it inherits their correctness."""
    logical_h(qc, q, b)
    logical_cnot(qc, q, a, b)
    logical_h(qc, q, b)


def apply_pauli_frame(qc, q, blk, x_mask: np.ndarray, z_mask: np.ndarray) -> None:
    """Physically apply a decoded Pauli correction to the data qubits."""
    for i, bit in enumerate(x_mask):
        if bit:
            qc.x(q[blk.data[i]])
    for i, bit in enumerate(z_mask):
        if bit:
            qc.z(q[blk.data[i]])


def syndrome_round(qc, q, c, blk, bit_base: int) -> None:
    """One unflagged syndrome round: 3 Z-stabilisers then 3 X-stabilisers.

    Z-stabilisers detect X errors, X-stabilisers detect Z errors.  Clbits are
    laid out as [z0 z1 z2 x0 x1 x2] starting at ``bit_base``.
    """
    for i, support in enumerate(STEANE_STAB):
        anc = q[blk.anc_z[i]]
        qc.reset(anc)
        for d in support:
            qc.cx(q[blk.data[d]], anc)
        qc.measure(anc, c[bit_base + i])
    for i, support in enumerate(STEANE_STAB):
        anc = q[blk.anc_x[i]]
        qc.reset(anc)
        qc.h(anc)
        for d in support:
            qc.cx(anc, q[blk.data[d]])
        qc.h(anc)
        qc.measure(anc, c[bit_base + N_STAB + i])


def measure_data(qc, q, c, blk, bit_base: int) -> None:
    for i, d in enumerate(blk.data):
        qc.measure(q[d], c[bit_base + i])


# --------------------------------------------------------------------------
# Logical program
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class LogicalGate:
    name: str
    targets: tuple[int, ...]

    def __str__(self) -> str:
        return f"{self.name}{''.join(str(t) for t in self.targets)}"


def parse_program(spec: str, n_logical: int = N_LOGICAL) -> list[list[LogicalGate]]:
    """Parse 'h0,cx01;cz12;x2' into segments of logical gates.

    Segments are separated by ';', gates within a segment by ',' or whitespace.
    One feedback round is run per segment.
    """
    one_q = {"h": logical_h, "x": logical_x, "z": logical_z}
    two_q = {"cx": logical_cnot, "cnot": logical_cnot, "cz": logical_cz}
    segments: list[list[LogicalGate]] = []
    for chunk in spec.split(";"):
        gates: list[LogicalGate] = []
        for token in chunk.replace(",", " ").split():
            token = token.strip().lower()
            if not token:
                continue
            name = token.rstrip("0123456789")
            digits = token[len(name):]
            if name in two_q:
                if len(digits) != 2:
                    raise ValueError(f"{token!r}: two-qubit gate needs two targets")
            elif name in one_q:
                if len(digits) != 1:
                    raise ValueError(f"{token!r}: single-qubit gate needs one target")
            else:
                raise ValueError(f"unknown logical gate {token!r}")
            targets = tuple(int(d) for d in digits)
            for t in targets:
                if not 0 <= t < n_logical:
                    raise ValueError(f"{token!r}: logical qubit {t} out of range")
            if len(set(targets)) != len(targets):
                raise ValueError(f"{token!r}: repeated target")
            gates.append(LogicalGate(name, targets))
        if gates:
            segments.append(gates)
    if not segments:
        raise ValueError("empty program")
    return segments


def apply_gate(qc, q, blocks, gate: LogicalGate) -> None:
    if gate.name in ("h",):
        logical_h(qc, q, blocks[gate.targets[0]])
    elif gate.name == "x":
        logical_x(qc, q, blocks[gate.targets[0]])
    elif gate.name == "z":
        logical_z(qc, q, blocks[gate.targets[0]])
    elif gate.name in ("cx", "cnot"):
        logical_cnot(qc, q, blocks[gate.targets[0]], blocks[gate.targets[1]])
    elif gate.name == "cz":
        logical_cz(qc, q, blocks[gate.targets[0]], blocks[gate.targets[1]])
    else:  # pragma: no cover
        raise ValueError(f"unhandled gate {gate}")


# --------------------------------------------------------------------------
# Clbit map
# --------------------------------------------------------------------------

SYN_BITS_PER_BLOCK = 2 * N_STAB


def syndrome_bit(block: int, basis: str, i: int) -> int:
    return block * SYN_BITS_PER_BLOCK + (0 if basis == "z" else N_STAB) + i


def data_bit(block: int, i: int, n_logical: int = N_LOGICAL) -> int:
    return n_logical * SYN_BITS_PER_BLOCK + block * N_DATA + i


def total_clbits(n_logical: int = N_LOGICAL) -> int:
    return n_logical * (SYN_BITS_PER_BLOCK + N_DATA)


# --------------------------------------------------------------------------
# Circuit assembly
# --------------------------------------------------------------------------


def parse_injections(specs: Sequence[str], n_logical: int) -> list[tuple[int, int, str]]:
    """Parse 'block:dataqubit:P' (P in X,Z,Y) into a list of static faults."""
    out: list[tuple[int, int, str]] = []
    for spec in specs:
        parts = spec.split(":")
        if len(parts) != 3:
            raise ValueError(f"--inject {spec!r}: expected block:qubit:pauli")
        b, q, pauli = int(parts[0]), int(parts[1]), parts[2].upper()
        if not 0 <= b < n_logical:
            raise ValueError(f"--inject {spec!r}: block out of range")
        if not 0 <= q < N_DATA:
            raise ValueError(f"--inject {spec!r}: data qubit must be 0..6")
        if pauli not in ("X", "Y", "Z"):
            raise ValueError(f"--inject {spec!r}: pauli must be X, Y or Z")
        out.append((b, q, pauli))
    return out


def apply_injections(qc, q, blocks, injections: Sequence[tuple[int, int, str]]) -> None:
    """Deterministic faults, for showing the feedback loop actually correcting."""
    for b, dq, pauli in injections:
        target = q[blocks[b].data[dq]]
        if pauli in ("X", "Y"):
            qc.x(target)
        if pauli in ("Z", "Y"):
            qc.z(target)


def build_round_circuit(
    segments: Sequence[Sequence[LogicalGate]],
    upto: int,
    frames: Sequence[dict[str, np.ndarray]],
    blocks: Sequence[BlockLayout],
    n_physical: int,
    final: bool,
    injections: Sequence[tuple[int, int, str]] = (),
) -> QuantumCircuit:
    """Circuit for feedback round ``upto``.

    Encodes all blocks, replays segments 0..upto-1 with their decoded
    corrections applied, runs segment ``upto``, then extracts one syndrome
    round.  On the last round the data qubits are measured too.
    """
    n_logical = len(blocks)
    q = QuantumRegister(n_physical, "q")
    c = ClassicalRegister(total_clbits(n_logical), "c")
    qc = QuantumCircuit(q, c)

    for blk in blocks:
        encode_zero(qc, q, blk)
    if injections:
        apply_injections(qc, q, blocks, injections)
    qc.barrier()

    for step in range(upto + 1):
        for gate in segments[step]:
            apply_gate(qc, q, blocks, gate)
        # Feed forward whatever the decoder already learned about this step.
        if step < len(frames):
            for b, blk in enumerate(blocks):
                frame = frames[step][b] if isinstance(frames[step], list) else None
                if frame is not None:
                    apply_pauli_frame(qc, q, blk, frame["x"], frame["z"])
        qc.barrier()

    for b, blk in enumerate(blocks):
        syndrome_round(qc, q, c, blk, b * SYN_BITS_PER_BLOCK)
    if final:
        qc.barrier()
        for b, blk in enumerate(blocks):
            measure_data(qc, q, c, blk, data_bit(b, 0, n_logical))
    return qc


# --------------------------------------------------------------------------
# Backends
# --------------------------------------------------------------------------


@dataclass
class Backend:
    name: str
    backend: Any
    n_qubits: int
    supports_dynamic: bool
    is_simulator: bool
    needs_iqm_routing: bool


def load_backend(
    spec: str,
    base_url: str | None,
    token: str | None,
    n_physical: int,
    p1: float = 1e-3,
    p2: float = 5e-3,
    p_meas: float = 5e-3,
) -> Backend:
    """Three kinds of backend.

    ``stabilizer``  Aer's Clifford simulator with a depolarising noise model.
                    The whole protocol is Clifford, so this scales to the 39
                    qubits the three blocks need. This is the offline default.
    ``aphrodite``   IQM's fake 54-qubit device. It is a *statevector* model of
                    the full device, so it cannot simulate this circuit; kept
                    only for small experiments and topology checks.
    anything else   a live IQM alias (e.g. ``emerald``) via IQMProvider.
    """
    if spec in ("stabilizer", "sim", "clifford"):
        from qiskit_aer import AerSimulator
        from qiskit_aer.noise import NoiseModel, depolarizing_error, pauli_error

        noise = NoiseModel()
        if p1 > 0:
            noise.add_all_qubit_quantum_error(
                depolarizing_error(p1, 1), ["h", "x", "z", "id", "s", "sdg"]
            )
        if p2 > 0:
            noise.add_all_qubit_quantum_error(
                depolarizing_error(p2, 2), ["cx", "cz", "swap"]
            )
        if p_meas > 0:
            noise.add_all_qubit_quantum_error(
                pauli_error([("X", p_meas), ("I", 1 - p_meas)]), ["measure"]
            )
        be = AerSimulator(method="stabilizer", noise_model=noise)
        return Backend(
            name=f"AerSimulator(stabilizer, p1={p1}, p2={p2}, p_meas={p_meas})",
            backend=be,
            n_qubits=max(n_physical, 1),
            supports_dynamic=True,
            is_simulator=True,
            needs_iqm_routing=False,
        )

    if spec in ("aphrodite", "fake", "emerald-fake"):
        from iqm.qiskit_iqm import IQMFakeAphrodite

        be = IQMFakeAphrodite()
        return Backend(
            name="IQMFakeAphrodite",
            backend=be,
            n_qubits=be.num_qubits,
            supports_dynamic="if_else" in be.target.operation_names,
            is_simulator=True,
            needs_iqm_routing=True,
        )

    from iqm.qiskit_iqm.iqm_provider import IQMProvider

    url = base_url or os.environ.get("IQM_BASE_URL") or "https://resonance.iqm.tech"
    # iqm-client refuses a token supplied both as an argument and through the
    # environment ("parameter sources must not be mixed"), so route an explicit
    # --token via the environment and never pass it as a kwarg.
    if token:
        os.environ["IQM_TOKEN"] = token
        os.environ.pop("RESONANCE_API_KEY", None)
    if not (os.environ.get("IQM_TOKEN") or os.environ.get("RESONANCE_API_KEY")):
        sys.exit(
            "no IQM credentials: set IQM_TOKEN (or RESONANCE_API_KEY), or pass --token"
        )
    provider = IQMProvider(url=url, quantum_computer=spec)
    try:
        be = provider.get_backend(use_metrics=True)
    except TypeError:
        be = provider.get_backend()
    ops = set(getattr(be.target, "operation_names", []))
    return Backend(
        name=f"{spec} @ {url}",
        backend=be,
        n_qubits=be.num_qubits,
        supports_dynamic="if_else" in ops,
        is_simulator=False,
        needs_iqm_routing=True,
    )


def run_circuit(
    bk: Backend, qc: QuantumCircuit, shots: int, layout: Sequence[int]
) -> list[str]:
    """Route for the device (real IQM) or compile for the simulator, then run."""
    if bk.needs_iqm_routing:
        from iqm.qiskit_iqm.iqm_naive_move_pass import transpile_to_IQM

        circuit = transpile_to_IQM(qc, bk.backend, initial_layout=list(layout))
    else:
        from qiskit import transpile

        circuit = transpile(qc, bk.backend, optimization_level=0)

    job = bk.backend.run(circuit, shots=shots, memory=True)
    result = job.result()
    try:
        return list(result.get_memory())
    except Exception:
        # Backend withheld per-shot memory; expand aggregated counts instead.
        memory: list[str] = []
        for bitstring, n in result.get_counts().items():
            memory.extend([bitstring] * n)
        return memory


def bits_of(shot: str, n_clbits: int) -> np.ndarray:
    """Qiskit puts clbit 0 rightmost; return an array indexed by clbit."""
    flat = shot.replace(" ", "")
    arr = np.zeros(n_clbits, dtype=np.uint8)
    for i, ch in enumerate(reversed(flat)):
        if i < n_clbits:
            arr[i] = 1 if ch == "1" else 0
    return arr


# --------------------------------------------------------------------------
# The real-time loop
# --------------------------------------------------------------------------


def logical_value(
    data_bits: np.ndarray, x_frame: np.ndarray, decoder: SteaneRestrictionDecoder
) -> tuple[int, np.ndarray]:
    """Correct a destructive Z-basis readout and return the logical bit."""
    corrected = (data_bits ^ x_frame) % 2
    residual = decoder.decode(syndrome_of(corrected))
    corrected = (corrected ^ residual) % 2
    return int(corrected.sum() % 2), corrected


def run_realtime_loop(
    segments: Sequence[Sequence[LogicalGate]],
    bk: Backend,
    blocks: Sequence[BlockLayout],
    decoder: SteaneRestrictionDecoder,
    shots: int,
    verbose: bool = True,
    injections: Sequence[tuple[int, int, str]] = (),
) -> dict[str, Any]:
    n_logical = len(blocks)
    n_clbits = total_clbits(n_logical)
    physical = [qi for blk in blocks for qi in blk.all_qubits]
    n_physical = max(physical) + 1

    frames: list[list[dict[str, np.ndarray]]] = []
    report: dict[str, Any] = {
        "mode": "loop",
        "backend": bk.name,
        "shots": shots,
        "n_logical": n_logical,
        "physical_qubits_used": len(physical),
        "device_qubits": bk.n_qubits,
        "program": [[str(g) for g in seg] for seg in segments],
        "rounds": [],
    }

    for r, segment in enumerate(segments):
        final = r == len(segments) - 1
        qc = build_round_circuit(
            segments, r, frames, blocks, n_physical, final, injections
        )
        if verbose:
            gates = " ".join(str(g) for g in segment)
            print(f"\n=== round {r}: apply [{gates}] "
                  f"({'final, data measured' if final else 'syndrome only'}) ===")
            print(f"  logical depth so far: {r + 1} segment(s); circuit depth {qc.depth()}")

        memory = run_circuit(bk, qc, shots, physical)

        # Decode every shot with PyMatching. Two things come out of this:
        #  * a per-shot Pauli frame, used to interpret that same shot's readout
        #    (this is ordinary Pauli-frame tracking, and it is exact per shot);
        #  * a feed-forward correction physically compiled into the next round's
        #    circuit. One circuit serves every shot, so this can only carry a
        #    *systematic* error -- the correction the majority of shots agree
        #    on. Under independent noise that is identity, which is the correct
        #    answer, and it becomes non-trivial when a qubit is genuinely stuck.
        round_entry: dict[str, Any] = {"round": r, "segment": [str(g) for g in segment],
                                       "blocks": []}
        new_frame: list[dict[str, np.ndarray]] = []
        shot_frames: list[list[dict[str, np.ndarray]]] = [[] for _ in memory]
        for b in range(n_logical):
            x_votes: Counter = Counter()
            z_votes: Counter = Counter()
            nontrivial = 0
            corrected_shots = 0
            for si, shot in enumerate(memory):
                bits = bits_of(shot, n_clbits)
                s_z = np.array([bits[syndrome_bit(b, "z", i)] for i in range(N_STAB)])
                s_x = np.array([bits[syndrome_bit(b, "x", i)] for i in range(N_STAB)])
                x_corr = decoder.decode(s_z)
                z_corr = decoder.decode(s_x)
                if s_z.any() or s_x.any():
                    nontrivial += 1
                if x_corr.any() or z_corr.any():
                    corrected_shots += 1
                shot_frames[si].append({"x": x_corr, "z": z_corr})
                x_votes[tuple(int(v) for v in x_corr)] += 1
                z_votes[tuple(int(v) for v in z_corr)] += 1
            x_mask = np.array(x_votes.most_common(1)[0][0], dtype=np.uint8)
            z_mask = np.array(z_votes.most_common(1)[0][0], dtype=np.uint8)
            new_frame.append({"x": x_mask, "z": z_mask})
            round_entry["blocks"].append(
                {
                    "block": b,
                    "feed_forward_x": [int(v) for v in x_mask],
                    "feed_forward_z": [int(v) for v in z_mask],
                    "nontrivial_syndrome_shots": nontrivial,
                    "shots_given_a_correction": corrected_shots,
                    "shots": len(memory),
                }
            )
            if verbose:
                ff = ("identity (no systematic error)"
                      if not (x_mask.any() or z_mask.any())
                      else f"X{list(int(v) for v in x_mask)} Z{list(int(v) for v in z_mask)}")
                print(
                    f"  block {b}: {nontrivial:4d}/{len(memory)} shots flagged a syndrome, "
                    f"{corrected_shots:4d} got a per-shot correction; feed-forward {ff}"
                )
        frames.append(new_frame)
        report["rounds"].append(round_entry)

        if final:
            logical_counts: Counter = Counter()
            per_block_ones = [0] * n_logical
            for si, shot in enumerate(memory):
                bits = bits_of(shot, n_clbits)
                word = []
                for b in range(n_logical):
                    raw = np.array(
                        [bits[data_bit(b, i, n_logical)] for i in range(N_DATA)],
                        dtype=np.uint8,
                    )
                    # This shot's own decoded frame, not the modal one.
                    val, _ = logical_value(raw, shot_frames[si][b]["x"], decoder)
                    per_block_ones[b] += val
                    word.append(str(val))
                logical_counts["".join(word)] += 1
            report["logical_counts"] = dict(logical_counts)
            report["per_logical_one_fraction"] = [
                round(n / max(1, len(memory)), 4) for n in per_block_ones
            ]
            if verbose:
                print("\n=== final logical readout (L0L1L2, per-shot Steane-corrected) ===")
                for word, n in sorted(logical_counts.items(), key=lambda kv: -kv[1]):
                    print(f"  {word}  {n:6d}  {n / len(memory):6.2%}")

    report["decoder_calls"] = dict(decoder.stats)
    return report


# --------------------------------------------------------------------------
# In-circuit dynamic mode
# --------------------------------------------------------------------------


def build_dynamic_circuit(
    segments: Sequence[Sequence[LogicalGate]],
    blocks: Sequence[BlockLayout],
    decoder: SteaneRestrictionDecoder,
    n_physical: int,
    injections: Sequence[tuple[int, int, str]] = (),
) -> QuantumCircuit:
    """One circuit with real conditional feedback after every syndrome round.

    The decoder is compiled to a lookup table and emitted as ``if_test`` blocks,
    which is how a real-time control stack handles a distance-3 code: PyMatching
    runs offline to build the table, the hardware applies it within the shot.
    """
    n_logical = len(blocks)
    q = QuantumRegister(n_physical, "q")
    table = decoder.lookup_table()

    syn_regs: list[list[ClassicalRegister]] = []
    for r in range(len(segments)):
        per_round = []
        for b in range(n_logical):
            per_round.append(ClassicalRegister(N_STAB, f"sz{r}_{b}"))
            per_round.append(ClassicalRegister(N_STAB, f"sx{r}_{b}"))
        syn_regs.append(per_round)
    data_regs = [ClassicalRegister(N_DATA, f"d{b}") for b in range(n_logical)]

    qc = QuantumCircuit(q, *[reg for rnd in syn_regs for reg in rnd], *data_regs)

    for blk in blocks:
        encode_zero(qc, q, blk)
    if injections:
        apply_injections(qc, q, blocks, injections)
    qc.barrier()

    for r, segment in enumerate(segments):
        for gate in segment:
            apply_gate(qc, q, blocks, gate)
        qc.barrier()
        for b, blk in enumerate(blocks):
            cz_reg = syn_regs[r][2 * b]
            cx_reg = syn_regs[r][2 * b + 1]
            # Measurement leaves an ancilla in the measured |0> or |1> state.
            # Dynamic mode reuses the same ancillas in later rounds, so restore
            # the |0> state required by both syndrome-extraction circuits.
            for anc in blk.anc_z + blk.anc_x:
                qc.reset(q[anc])
            for i, support in enumerate(STEANE_STAB):
                anc = q[blk.anc_z[i]]
                qc.reset(anc)
                for d in support:
                    qc.cx(q[blk.data[d]], anc)
                qc.measure(anc, cz_reg[i])
            for i, support in enumerate(STEANE_STAB):
                anc = q[blk.anc_x[i]]
                qc.reset(anc)
                qc.h(anc)
                for d in support:
                    qc.cx(anc, q[blk.data[d]])
                qc.h(anc)
                qc.measure(anc, cx_reg[i])
            # Real-time feed-forward from the compiled decoder table.
            for value, correction in table.items():
                if not correction.any():
                    continue
                with qc.if_test((cz_reg, value)):
                    for i, bit in enumerate(correction):
                        if bit:
                            qc.x(q[blk.data[i]])
                with qc.if_test((cx_reg, value)):
                    for i, bit in enumerate(correction):
                        if bit:
                            qc.z(q[blk.data[i]])
        qc.barrier()

    for b, blk in enumerate(blocks):
        for i, d in enumerate(blk.data):
            qc.measure(q[d], data_regs[b][i])
    return qc


def run_dynamic(
    segments, bk: Backend, blocks, decoder, shots: int, verbose: bool = True,
    injections: Sequence[tuple[int, int, str]] = (),
) -> dict[str, Any]:
    n_logical = len(blocks)
    n_rounds = len(segments)
    physical = [qi for blk in blocks for qi in blk.all_qubits]
    qc = build_dynamic_circuit(
        segments, blocks, decoder, max(physical) + 1, injections
    )
    n_cond = sum(1 for inst in qc.data if inst.operation.name == "if_else")
    if verbose:
        print(f"dynamic circuit: depth {qc.depth()}, {n_cond} conditional blocks "
              f"({n_rounds} syndrome rounds x {n_logical} blocks, corrections applied "
              f"in-circuit from the compiled decoder table)")
    memory = run_circuit(bk, qc, shots, physical)

    # Clbit layout: all syndrome registers in creation order, then the data
    # registers. bits_of() indexes clbits directly, so offsets are explicit.
    syn_total = n_rounds * n_logical * 2 * N_STAB
    n_clbits = syn_total + n_logical * N_DATA

    logical_counts: Counter = Counter()
    per_block_ones = [0] * n_logical
    residual_fixups = 0
    for shot in memory:
        bits = bits_of(shot, n_clbits)
        word = []
        for b in range(n_logical):
            raw = np.array(
                [bits[syn_total + b * N_DATA + i] for i in range(N_DATA)],
                dtype=np.uint8,
            )
            # Corrections already happened in hardware; only the readout-time
            # residual is left to decode.
            if syndrome_of(raw).any():
                residual_fixups += 1
            val, _ = logical_value(raw, np.zeros(N_DATA, dtype=np.uint8), decoder)
            per_block_ones[b] += val
            word.append(str(val))
        logical_counts["".join(word)] += 1

    if verbose:
        print(f"  readout-time residual syndromes: {residual_fixups} "
              f"(over {len(memory)} shots x {n_logical} blocks)")
        print("\n=== final logical readout (L0L1L2, in-circuit corrected) ===")
        for word, n in sorted(logical_counts.items(), key=lambda kv: -kv[1]):
            print(f"  {word}  {n:6d}  {n / max(1, len(memory)):6.2%}")

    return {
        "mode": "dynamic",
        "backend": bk.name,
        "shots": shots,
        "program": [[str(g) for g in seg] for seg in segments],
        "conditional_blocks": n_cond,
        "logical_counts": dict(logical_counts),
        "per_logical_one_fraction": [
            round(n / max(1, len(memory)), 4) for n in per_block_ones
        ],
        "readout_residual_syndromes": residual_fixups,
        "decoder_calls": dict(decoder.stats),
    }


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Real-time FTQC with 3 Steane logical qubits on IQM Emerald",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--backend", default="stabilizer",
                   help="'stabilizer' for the offline Clifford simulator (default), "
                        "'aphrodite' for IQM's fake 54-qubit statevector device, "
                        "or a live IQM alias such as 'emerald'")
    p.add_argument("--p1", type=float, default=1e-3,
                   help="simulator 1-qubit depolarising rate (default 1e-3)")
    p.add_argument("--p2", type=float, default=5e-3,
                   help="simulator 2-qubit depolarising rate (default 5e-3)")
    p.add_argument("--p-meas", type=float, default=5e-3,
                   help="simulator measurement flip rate (default 5e-3)")
    p.add_argument("--base-url", default=None,
                   help="IQM server URL (default: $IQM_BASE_URL or Resonance)")
    p.add_argument("--token", default=None,
                   help="IQM token (default: $IQM_TOKEN or $RESONANCE_API_KEY)")
    p.add_argument("--mode", choices=("loop", "dynamic"), default="loop",
                   help="host-in-the-loop feedback, or in-circuit if_test feedback")
    p.add_argument("--program", default="h0;cx01;cz12;x2",
                   help="logical circuit, segments separated by ';' "
                        "(default: h0;cx01;cz12;x2)")
    p.add_argument("--shots", type=int, default=512, help="shots per round")
    p.add_argument("--logical-qubits", type=int, default=N_LOGICAL)
    p.add_argument("--inject", action="append", default=[], metavar="B:Q:P",
                   help="inject a static Pauli fault on block B data qubit Q "
                        "(e.g. 0:3:X). Repeatable. Shows the feed-forward "
                        "correction firing instead of staying identity.")
    p.add_argument("--self-test", action="store_true",
                   help="verify the decoder exhaustively and exit")
    p.add_argument("--allow-fallback", action="store_true",
                   help="in dynamic mode, fall back to loop mode if the backend "
                        "has no conditional branching")
    p.add_argument("--results-json", type=Path, default=None,
                   help="write the full report to this path")
    p.add_argument("--quiet", action="store_true")
    return p.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    verbose = not args.quiet
    decoder = SteaneRestrictionDecoder()

    if args.self_test:
        print("Steane restriction decoder self-test (PyMatching x3 + exact lift)")
        ok = decoder.self_test(verbose=True)
        print(f"\ndecoder paths used: {dict(decoder.stats)}")
        print("SELF-TEST:", "PASS" if ok else "FAIL")
        return 0 if ok else 1

    try:
        segments = parse_program(args.program, args.logical_qubits)
    except ValueError as exc:
        sys.exit(f"--program: {exc}")
    blocks = build_layout(args.logical_qubits)
    needed = sum(len(b.all_qubits) for b in blocks)
    n_physical = max(qi for blk in blocks for qi in blk.all_qubits) + 1

    if verbose:
        print(f"program: {' ; '.join(' '.join(str(g) for g in s) for s in segments)}")
        print(f"{args.logical_qubits} logical qubits, Steane [[7,1,3]], "
              f"{needed} physical qubits "
              f"({N_DATA} data + {2 * N_STAB} ancilla per block)")

    bk = load_backend(
        args.backend, args.base_url, args.token, n_physical,
        p1=args.p1, p2=args.p2, p_meas=args.p_meas,
    )
    if verbose:
        print(f"backend: {bk.name} ({bk.n_qubits} qubits, "
              f"dynamic circuits: {'yes' if bk.supports_dynamic else 'no'})")
    if needed > bk.n_qubits:
        sys.exit(f"need {needed} qubits but {bk.name} has {bk.n_qubits}")

    mode = args.mode
    if mode == "dynamic" and not bk.supports_dynamic:
        msg = (f"{bk.name} does not advertise conditional branching (if_else), "
               f"so in-circuit real-time feedback cannot be compiled for it.")
        if not args.allow_fallback:
            sys.exit(msg + "\nRe-run with --mode loop, or --allow-fallback.")
        print(msg + " Falling back to --mode loop.")
        mode = "loop"

    if args.backend in ("aphrodite", "fake", "emerald-fake") and needed > 24:
        sys.exit(
            f"{bk.name} is a statevector model of the whole {bk.n_qubits}-qubit device, "
            f"so it cannot simulate a {needed}-qubit circuit.\n"
            f"Use --backend stabilizer for offline runs (the protocol is Clifford), "
            f"or point --backend at a real device such as 'emerald'."
        )

    try:
        injections = parse_injections(args.inject, args.logical_qubits)
    except ValueError as exc:
        sys.exit(str(exc))
    if injections and verbose:
        pretty = ", ".join(f"{p} on block {b} data q{q}" for b, q, p in injections)
        print(f"injected static faults: {pretty}")

    if mode == "dynamic":
        report = run_dynamic(
            segments, bk, blocks, decoder, args.shots, verbose, injections
        )
    else:
        report = run_realtime_loop(
            segments, bk, blocks, decoder, args.shots, verbose, injections
        )
    report["injections"] = [f"{b}:{q}:{p}" for b, q, p in injections]

    if args.results_json:
        args.results_json.write_text(json.dumps(report, indent=2))
        if verbose:
            print(f"\nwrote {args.results_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
