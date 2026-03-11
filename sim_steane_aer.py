#!/usr/bin/env python3
"""
Steane [[7,1,3]] Physical Circuit Simulation via Qiskit Aer
============================================================
Simulates fault-tolerant circuits encoded in the Steane 7-qubit code.
The encoding is derived from the FTQC dialect's Steane ECC annotation:

  !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

Physical circuit construction (no FTQC pass does this automatically yet):
  - 1 logical qubit → 7 physical data qubits
  - Encoding circuit: H on ancilla qubits 0,1,3 + CNOT cascade
  - Logical H → transversal H (H on all 7 physical qubits)
  - Logical CNOT → transversal CNOT (CNOT on corresponding pairs)
  - Logical measure → majority vote (or equivalently, parity of 7 bits)

Circuits simulated:
  1. Bell state   (14 physical qubits) — Qiskit Aer statevector
  2. GHZ-4 state  (28 physical qubits) — Qiskit Aer stabilizer (Clifford)

Expected outcomes (matched to logical QASM3 from ftqc-opt):
  Bell:  50% |00>_L + 50% |11>_L
  GHZ-4: 50% |0000>_L + 50% |1111>_L
"""

import sys
from qiskit import QuantumCircuit, transpile
from qiskit_aer import AerSimulator

SHOTS = 8192

# ─── Steane [[7,1,3]] Code Primitives ────────────────────────────────────────

def steane_encode_zero(qc: QuantumCircuit, block: list[int]) -> None:
    """
    Encode logical |0_L> from physical |0000000> on the 7 qubits in `block`.

    Parity check matrix of the [7,4,3] Hamming code (3 rows × 7 cols):
      row0 = [0,0,0,1,1,1,1]  → X stabiliser g1 = X on {3,4,5,6}
      row1 = [0,1,1,0,0,1,1]  → X stabiliser g2 = X on {1,2,5,6}
      row2 = [1,0,1,0,1,0,1]  → X stabiliser g3 = X on {0,2,4,6}

    Encoding: H on check qubits (0,1,3), then CNOT cascade to data qubits.
    Resulting state: 1/√8 Σ_{a,b,c∈{0,1}} |a,b,a⊕b,c,a⊕c,b⊕c,a⊕b⊕c>
    = |0_L> (all 8 even-weight Hamming codewords with equal amplitude).
    """
    q = block
    # H on check qubits to create superposition of stabiliser generators
    qc.h(q[0])  # g3 check
    qc.h(q[1])  # g2 check
    qc.h(q[3])  # g1 check
    # Spread from q[0] (g3: positions 0,2,4,6)
    qc.cx(q[0], q[2]); qc.cx(q[0], q[4]); qc.cx(q[0], q[6])
    # Spread from q[1] (g2: positions 1,2,5,6)
    qc.cx(q[1], q[2]); qc.cx(q[1], q[5]); qc.cx(q[1], q[6])
    # Spread from q[3] (g1: positions 3,4,5,6)
    qc.cx(q[3], q[4]); qc.cx(q[3], q[5]); qc.cx(q[3], q[6])


def decode_logical(bits_7: list[int]) -> int:
    """
    Decode logical qubit from 7 physical measurements.

    Logical Z_L = Z^⊗7 → eigenvalue is (-1)^(parity of all 7 bits).
    |0_L> codewords all have even parity (0 or 4 ones).
    |1_L> codewords all have odd  parity (3 or 7 ones).
    So: logical bit = XOR of all 7 physical bits.
    """
    return sum(bits_7) % 2


def print_separator(title: str) -> None:
    print("\n" + "=" * 62)
    print(f"  {title}")
    print("=" * 62)


# ─── Circuit 1: Bell State (14 physical qubits) ───────────────────────────────

def run_steane_bell(shots: int = SHOTS) -> None:
    print_separator("Steane Bell State — 14 physical qubits (Aer statevector)")

    qc = QuantumCircuit(14, 14)

    # Encode both logical qubits as |0_L>
    steane_encode_zero(qc, list(range(0, 7)))   # Block 0: q0-q6
    steane_encode_zero(qc, list(range(7, 14)))  # Block 1: q7-q13

    # Logical H on block 0 → transversal H on all 7 physical qubits
    for i in range(7):
        qc.h(i)

    # Logical CNOT (block0 ctrl, block1 tgt) → 7 transversal CNOTs
    for i in range(7):
        qc.cx(i, i + 7)

    qc.measure(range(14), range(14))

    print(f"  Circuit depth : {qc.depth()}")
    print(f"  Physical qubits: 14  (2 logical × 7)")
    print(f"  Gate count    : {qc.count_ops()}")

    sim = AerSimulator(method='statevector')
    t = transpile(qc, sim)
    result = sim.run(t, shots=shots).result()
    raw_counts = result.get_counts()

    # Decode logical values using parity
    logical_counts: dict[str, int] = {}
    for bitstr, cnt in raw_counts.items():
        # Qiskit bitstring: leftmost char = highest qubit index
        bits = [int(b) for b in reversed(bitstr)]  # bits[i] = qubit i
        l0 = decode_logical(bits[0:7])   # block 0
        l1 = decode_logical(bits[7:14])  # block 1
        key = f"|{l0}{l1}>"
        logical_counts[key] = logical_counts.get(key, 0) + cnt

    total = sum(logical_counts.values())
    print(f"\n  Logical measurement results ({shots} shots):")
    for state in sorted(logical_counts):
        cnt = logical_counts[state]
        print(f"    {state}: {cnt:6d}  ({100*cnt/total:.1f}%)")

    # Verify: only |00> and |11> should appear
    unexpected = {k for k in logical_counts if k not in ("|00>", "|11>")}
    if unexpected:
        print(f"\n  FAIL: unexpected logical states: {unexpected}")
        sys.exit(1)

    p00 = logical_counts.get("|00>", 0) / total
    p11 = logical_counts.get("|11>", 0) / total
    if not (0.45 < p00 < 0.55 and 0.45 < p11 < 0.55):
        print(f"\n  FAIL: Bell probabilities out of range: P(|00>)={p00:.3f}, P(|11>)={p11:.3f}")
        sys.exit(1)

    print(f"\n  PASS  Only |00>_L and |11>_L observed with ~50/50 probability")
    print(f"        Matches logical QASM3:  h q0; cx q0,q1;  →  Bell state")


# ─── Circuit 2: GHZ-4 State (28 physical qubits, stabilizer sim) ─────────────

def run_steane_ghz4(shots: int = SHOTS) -> None:
    print_separator("Steane GHZ-4 State — 28 physical qubits (Aer stabilizer)")

    qc = QuantumCircuit(28, 28)

    # Encode all 4 logical qubits as |0_L>
    for b in range(4):
        steane_encode_zero(qc, list(range(7 * b, 7 * b + 7)))

    # Logical H on block 0 → transversal H on q0..q6
    for i in range(7):
        qc.h(i)

    # Logical chained CNOTs: block0→1→2→3
    for b in range(3):
        for i in range(7):
            qc.cx(7 * b + i, 7 * (b + 1) + i)

    qc.measure(range(28), range(28))

    print(f"  Circuit depth : {qc.depth()}")
    print(f"  Physical qubits: 28  (4 logical × 7)")
    print(f"  Gate count    : {qc.count_ops()}")
    print(f"  Using Aer stabilizer method (all-Clifford circuit)")

    # Stabilizer simulator handles 28 Clifford qubits in O(n^2) — fast
    sim = AerSimulator(method='stabilizer')
    t = transpile(qc, sim)
    result = sim.run(t, shots=shots).result()
    raw_counts = result.get_counts()

    # Decode
    logical_counts: dict[str, int] = {}
    for bitstr, cnt in raw_counts.items():
        bits = [int(b) for b in reversed(bitstr)]
        logical = [decode_logical(bits[7 * b: 7 * b + 7]) for b in range(4)]
        key = "|" + "".join(str(x) for x in logical) + ">"
        logical_counts[key] = logical_counts.get(key, 0) + cnt

    total = sum(logical_counts.values())
    print(f"\n  Logical measurement results ({shots} shots):")
    for state in sorted(logical_counts):
        cnt = logical_counts[state]
        print(f"    {state}: {cnt:6d}  ({100*cnt/total:.1f}%)")

    unexpected = {k for k in logical_counts if k not in ("|0000>", "|1111>")}
    if unexpected:
        print(f"\n  FAIL: unexpected states: {unexpected}")
        sys.exit(1)

    p0 = logical_counts.get("|0000>", 0) / total
    p1 = logical_counts.get("|1111>", 0) / total
    if not (0.45 < p0 < 0.55 and 0.45 < p1 < 0.55):
        print(f"\n  FAIL: GHZ probabilities off: P(|0000>)={p0:.3f}, P(|1111>)={p1:.3f}")
        sys.exit(1)

    print(f"\n  PASS  Only |0000>_L and |1111>_L observed with ~50/50 probability")
    print(f"        Matches logical QASM3:  h q0; cx q0,q1; cx q1,q2; cx q2,q3;")


# ─── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("Steane [[7,1,3]] Physical Circuit Simulation — Qiskit Aer")
    print("Generated QASM3 circuits from: ftqc-opt --ftqc-lower-to-qasm3\n")
    print("Encoding:  1 logical qubit → 7 physical qubits")
    print("Decoding:  logical bit = parity(7 physical measurements)")

    run_steane_bell()
    run_steane_ghz4()

    print("\n" + "=" * 62)
    print("  ALL STEANE PHYSICAL SIMULATIONS PASSED")
    print("=" * 62 + "\n")
