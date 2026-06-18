"""Run an IQM-native JSON circuit offline on the noisy FakeGarnet simulator.

The JSON uses IQM's native instruction set:
  - prx     : phased RX rotation, args angle_t / phase_t given in *turns* (x 2pi)
  - cz      : controlled-Z
  - measure : single-qubit measurement, args.key names the result

Qubit naming
------------
On IQM hardware every qubit is physically named QB1..QB20; there is no separate
"ancilla" qubit type -- "ancilla" is just a role you assign. To use logical names
like "data_0" / "ancilla_0", add a "qubit_mapping" object to the JSON that maps
each logical name to a physical qubit, e.g.

    "qubit_mapping": { "data_0": "QB1", "ancilla_0": "QB4", "data_1": "QB5" }

The runner resolves physical names to backend indices via
backend.qubit_name_to_index(...) and pins the placement with initial_layout, so
the gates land on exactly the physical qubits you chose (pick connected pairs to
avoid routing). If no mapping is given it falls back to parsing "QB<n>" directly.

No server URL or authentication is required.
"""
import json
import math
import sys

from qiskit import QuantumCircuit, transpile
from iqm.qiskit_iqm.fake_backends.fake_garnet import IQMFakeGarnet

TURN = 2 * math.pi  # IQM angles are fractions of a full turn


def resolve_qubits(data: dict, backend):
    """Return (logical_order, logical->virtual_index, initial_layout).

    logical_order  : logical qubit names in a stable order
    name_to_vidx   : logical name  -> compact virtual index (0..n-1) in the circuit
    initial_layout : virtual index -> physical device index (for transpile)
    """
    insts = data["instructions"]
    mapping = data.get("qubit_mapping")  # logical name -> physical "QBk"

    # Logical names in first-seen order.
    logical_order = []
    for inst in insts:
        for q in inst["qubits"]:
            if q not in logical_order:
                logical_order.append(q)

    name_to_vidx = {name: i for i, name in enumerate(logical_order)}

    if mapping:
        # logical -> physical "QBk" -> device index
        initial_layout = [backend.qubit_name_to_index(mapping[name]) for name in logical_order]
    else:
        # Bare "QB<n>" names: place each on its own physical qubit.
        initial_layout = [backend.qubit_name_to_index(name) for name in logical_order]

    return logical_order, name_to_vidx, initial_layout


def build_circuit(data: dict, name_to_vidx: dict) -> QuantumCircuit:
    insts = data["instructions"]
    measures = [inst for inst in insts if inst["name"] == "measure"]

    qc = QuantumCircuit(len(name_to_vidx), len(measures), name=data.get("name", "circuit"))

    clbit = 0
    for inst in insts:
        name = inst["name"]
        qs = [name_to_vidx[q] for q in inst["qubits"]]
        args = inst.get("args", {})
        if name == "prx":
            qc.r(args["angle_t"] * TURN, args["phase_t"] * TURN, qs[0])
        elif name == "cz":
            qc.cz(qs[0], qs[1])
        elif name == "measure":
            qc.measure(qs[0], clbit)
            clbit += 1
        else:
            raise ValueError(f"Unsupported IQM instruction: {name!r}")
    return qc


def main(path: str) -> None:
    with open(path) as f:
        data = json.load(f)

    backend = IQMFakeGarnet()
    logical_order, name_to_vidx, initial_layout = resolve_qubits(data, backend)

    print(f"Loaded IQM circuit '{data.get('name')}' from {path}")
    print("Qubit mapping (logical -> physical -> device index):")
    mapping = data.get("qubit_mapping", {})
    for name in logical_order:
        phys = mapping.get(name, name)
        print(f"  {name:<12} -> {phys:<5} -> idx {backend.qubit_name_to_index(phys)}")

    qc = build_circuit(data, name_to_vidx)
    print(qc)
    print("------------------")
    tqc = transpile(qc, backend=backend, initial_layout=initial_layout)
    print(tqc)
    job = backend.run(tqc, shots=1000)
    print("Counts:", job.result().get_counts())


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "iqm-client.json")
