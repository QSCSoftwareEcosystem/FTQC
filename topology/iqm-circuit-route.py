#!/usr/bin/env python3
"""
iqm-circuit-route.py

Load an IQM circuit from a JSON file, reconstruct it as a Qiskit
QuantumCircuit, route/transpile it for an IQM backend, serialize the routed
circuit back to IQM JSON, and store routing-quality metrics in the output.

Example:
    python iqm-circuit-route.py --input-circuit inputCircuit.JSON
    python iqm-circuit-route.py --input-circuit inputCircuit.JSON \
        --token $IQM_TOKEN --base-url https://qccsw.ccs.ornl.gov \
    --qc-alias [my-quantum-computer] --shots 1024

The optional --shots argument is an integer number of backend shots. A
positive value submits the transpiled circuit to the backend; the default
value, 0, skips backend submission.
If the input filename contains "-transpiled" and shots is positive, submit
the input circuit without transpiling it again or writing another output file.

Output:
    inputCircuit-transpiled.JSON
    results-inputCircuit.JSON (when shots > 0), containing physical counts,
    logical_counts_raw, logical_counts_steane_corrected, and measurement order.
    Logical histogram strings list ascending logical IDs from left to right.
    Each logical block must have seven data measurements in Steane index order.

Expected input JSON structure:
{
  "name": "circuit-name",
  "instructions": [
    {
      "name": "prx",
      "locus": ["QB1"],
      "args": {"angle_t": 0.25, "phase_t": 0.0}
    }
  ]
}

PRX arguments accept angle_t/phase_t in turns or angle/phase in radians.

The IQM URL is selected by --base-url, then IQM_BASE_URL, and finally the
default URL. The quantum-computer alias is selected by --qc-alias, then
IQM_QC_ALIAS, and finally "default". The token is selected by --token, then
IQM_TOKEN.
"""

import argparse
import json
import math
import os
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

from qiskit import QuantumCircuit
from iqm.qiskit_iqm.iqm_naive_move_pass import transpile_to_IQM
from iqm.qiskit_iqm.iqm_provider import IQMProvider

DEBUG_PRINT = 0
_QUBIT_RE = re.compile(r"^QB(\d+)$", re.IGNORECASE)


_DATA_MEASUREMENT_RE = re.compile(r"^mL(\d+)(?:S\d+)?_QB\d+_\d+$")
STEANE_STAB = [[0, 2, 4, 6], [1, 2, 5, 6], [3, 4, 5, 6]]


def logical_measurement_results(
    counts: dict[str, int], key_to_cbit: dict[str, int],
) -> dict[str, Any]:
    """Decode seven-bit Steane readouts, weighted by shot count.

    Data positions 0..6 follow input measurement (classical-bit) order within
    each block, not hardware-qubit order, which routing can change. Count
    strings are Qiskit little endian. Output logical strings are ordered left
    to right by ascending logical ID; logical measurement is total parity.
    Correction assumes at most one flipped data bit per block; multiple errors
    can be miscorrected. Stabilizer measurements are never used or changed.
    """
    groups: dict[int, list[tuple[int, str]]] = {}
    for key, cbit in key_to_cbit.items():
        match = _DATA_MEASUREMENT_RE.fullmatch(key)
        if match:
            groups.setdefault(int(match.group(1)), []).append((cbit, key))
    logical_ids = sorted(groups)
    for logical_id in logical_ids:
        groups[logical_id].sort()
        if len(groups[logical_id]) != 7:
            raise ValueError(
                f"Logical qubit {logical_id} requires exactly 7 data measurements; "
                f"found {len(groups[logical_id])}. Repeated readout rounds must "
                "be separated before decoding."
            )

    raw: Counter[str] = Counter()
    corrected: Counter[str] = Counter()
    width = max(key_to_cbit.values(), default=-1) + 1
    for outcome, frequency in counts.items():
        bits = outcome.replace(" ", "")
        if len(bits) != width or any(bit not in "01" for bit in bits):
            raise ValueError(f"Invalid {width}-bit measurement outcome: {outcome!r}")
        raw_values = []
        corrected_values = []
        for logical_id in logical_ids:
            data = [int(bits[-1 - cbit]) for cbit, _ in groups[logical_id]]
            raw_values.append(str(sum(data) % 2))
            syndrome = [sum(data[i] for i in row) % 2 for row in STEANE_STAB]
            # The parity-check columns encode positions 1 through 7.
            error_position = sum(bit << i for i, bit in enumerate(syndrome))
            if error_position:
                data[error_position - 1] ^= 1
            corrected_values.append(str(sum(data) % 2))
        if logical_ids:
            raw["".join(raw_values)] += frequency
            corrected["".join(corrected_values)] += frequency

    return {
        "counts": counts,
        "key_to_cbit": key_to_cbit,
        "logical_qubit_order": logical_ids,
        "data_measurement_keys": {
            str(logical_id): [key for _, key in groups[logical_id]]
            for logical_id in logical_ids
        },
        "logical_counts_raw": dict(sorted(raw.items())),
        "logical_counts_steane_corrected": dict(sorted(corrected.items())),
    }


def write_measurement_results(
    input_path: Path, counts: dict[str, int], key_to_cbit: dict[str, int],
) -> None:
    results = logical_measurement_results(counts, key_to_cbit)
    with results_path_for(input_path).open("w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
        f.write("\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Route/transpile an IQM JSON circuit for an IQM backend."
    )
    parser.add_argument(
        "--input-circuit",
        required=True,
        type=Path,
        help="Input IQM circuit JSON file.",
    )
    parser.add_argument(
        "--base-url",
        default=None,
        help=(
            "IQM server URL (default: $IQM_BASE_URL or "
            "https://qccsw.ccs.ornl.gov)."
        ),
    )
    parser.add_argument(
        "--qc-alias",
        default=None,
        help="IQM quantum-computer alias (default: $IQM_QC_ALIAS or 'default').",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="IQM authentication token (default: $IQM_TOKEN).",
    )
    parser.add_argument(
        "--shots",
        type=int,
        default=0,
        help="Number of shots to submit (default: 0; use 0 to skip submission).",
    )
    return parser.parse_args()


def output_path_for(input_path: Path) -> Path:
    """Add '-transpiled' before the existing file extension."""
    return input_path.with_name(
        f"{input_path.stem}-transpiled{input_path.suffix}"
    )


def results_path_for(input_path: Path) -> Path:
    """Prefix the input filename with 'results-'."""
    return input_path.with_name(f"results-{input_path.name}")


def qubit_index(qubit_name: str) -> int:
    """
    Convert an IQM qubit name to its Qiskit index.

    Examples:
        QB1 -> 0
        QB3 -> 2
        QB7 -> 6
    """
    match = _QUBIT_RE.fullmatch(qubit_name)
    if match is None:
        raise ValueError(
            f"Unsupported qubit name {qubit_name!r}; expected names such as QB1, QB2, ..."
        )

    qnum = int(match.group(1))
    if qnum < 1:
        raise ValueError(f"Invalid IQM qubit name {qubit_name!r}")

    return qnum - 1


def build_quantum_circuit(
    circuit: dict[str, Any],
) -> tuple[QuantumCircuit, list[int], dict[str, int]]:
    instructions = circuit.get("instructions", [])
    if not isinstance(instructions, list):
        raise ValueError("'circuit.instructions' must be a JSON array")

    # Derive each Qiskit index directly from the numeric part of the IQM name.
    # Thus QB1 -> 0, QB3 -> 2, QB7 -> 6.
    used_qubits: set[str] = set()
    for ins in instructions:
        for component in ins.get("locus", []):
            # The input circuit handled here is expected to contain qubits.
            # Reject other component types (for example computational resonators)
            # rather than silently assigning them incorrect Qiskit qubit indices.
            qubit_index(component)
            used_qubits.add(component)

    if not used_qubits:
        raise ValueError("The input circuit does not contain any qubits")

    input_qubit_mapping = {q: qubit_index(q) for q in used_qubits}
    num_qubits = len(input_qubit_mapping)
    internal_qubit_mapping = {q: i for i, q in enumerate(sorted(used_qubits))}
    initial_layout = [input_qubit_mapping[q] for q in sorted(used_qubits)]

    nmeas = sum(1 for ins in instructions if ins.get("name", "").lower() == "measure")
    qc = QuantumCircuit(num_qubits, nmeas, name=circuit.get("name", "circuit"))

    classical_bit = 0
    key_to_cbit = {}

    for ins in instructions:
        name = ins.get("name", "").lower()
        args = ins.get("args", {})
        locus = ins.get("locus", [])
        loc = [internal_qubit_mapping[q] for q in locus]

        if name == "prx":
            if len(loc) != 1:
                raise ValueError(f"'prx' requires one qubit: {ins}")
            error = (
                "'prx' requires numeric 'angle_t' and 'phase_t' in turns "
                f"or 'angle' and 'phase' in radians: {ins}"
            )
            if not isinstance(args, dict):
                raise ValueError(error)
            if {"angle_t", "phase_t"}.issubset(args):
                angle_key, phase_key, scale = "angle_t", "phase_t", math.tau
            elif {"angle", "phase"}.issubset(args):
                angle_key, phase_key, scale = "angle", "phase", 1.0
            else:
                raise ValueError(error)
            try:
                angle = float(args[angle_key]) * scale
                phase = float(args[phase_key]) * scale
            except (TypeError, ValueError) as exc:
                raise ValueError(error) from exc
            qc.r(angle, phase, loc[0])

        elif name == "cz":
            if len(loc) != 2:
                raise ValueError(f"'cz' requires two qubits: {ins}")
            qc.cz(loc[0], loc[1])

        elif name == "measure":
            if len(loc) != 1:
                raise ValueError(f"'measure' requires one qubit: {ins}")
            qc.measure(loc[0], classical_bit)
            key_to_cbit[args.get("key", "c%d" % classical_bit)] = classical_bit
            classical_bit += 1

        elif name == "reset":
            if len(loc) != 1:
                raise ValueError(f"'reset' requires one qubit: {ins}")
            qc.reset(loc[0])

        else:
            raise ValueError(
                f"Unsupported input IQM instruction {ins.get('name')!r}. "
                "This script currently accepts prx, cz, reset, and measure."
            )

    return qc, initial_layout, key_to_cbit


def get_backend(url: str, token: str | None, alias: str):
    provider_kwargs = {"url": url, "quantum_computer": alias}
    if token is not None:
        provider_kwargs["token"] = token
    provider = IQMProvider(**provider_kwargs)

    # Newer qiskit-iqm versions can expose calibration/quality metrics in
    # backend.target through use_metrics=True.  Retain compatibility with
    # versions whose get_backend() does not accept that keyword.
    try:
        return provider.get_backend(use_metrics=True)
    except TypeError:
        return provider.get_backend()


def serialized_instruction_dict(op: Any) -> dict[str, Any]:
    return {
        "name": op.name,
        "locus": list(op.locus),
        "args": dict(op.args),
    }


def count_serialized_operations(serialized_instructions: list[dict[str, Any]]) -> dict[str, int]:
    """
    Compute operation-count metrics from the final serialized IQM circuit.

    two_qubit_gate_count:
        Counts serialized operations acting on exactly two components, except
        MOVE.  SWAP is included because it is a two-qubit operation.

    move_count and explicit_swap_count are also reported separately.
    """
    total_operation_count = len(serialized_instructions)

    move_count = sum(
        1 for ins in serialized_instructions
        if str(ins["name"]).lower() == "move"
    )

    explicit_swap_count = sum(
        1 for ins in serialized_instructions
        if str(ins["name"]).lower() == "swap"
    )

    two_qubit_gate_count = sum(
        1
        for ins in serialized_instructions
        if len(ins.get("locus", [])) == 2
        and str(ins["name"]).lower() != "move"
    )

    return {
        "total_operation_count": total_operation_count,
        "two_qubit_gate_count": two_qubit_gate_count,
        "move_count": move_count,
        "explicit_swap_count": explicit_swap_count,
    }


def _instruction_qubit_indices(circuit: QuantumCircuit, instruction: Any, initial_layout: list[int] | None = None) -> tuple[int, ...]:
    """Return the instruction qubit indices in backend hardware coordinates.

    The transpiled circuit may carry a final layout mapping virtual qubit
    indices to physical backend qubits.  Use that mapping when present so
    backend.target lookups use hardware qubit indices instead of the transient
    virtual indices used within the transpiled circuit.
    """
    # circuit.find_bit().index returns the virtual qubit index used in the transpiled QuantumCircuit.
    # qargs = tuple(circuit.find_bit(q).index for q in instruction.qubits)
    qargs: tuple[int, ...] = ()

    layout = getattr(circuit, "layout", None)
    if layout is None:
        if DEBUG_PRINT >= 2:
            print(f"[INFO] no layout in the transpiled circuit", file=sys.stderr)
        if initial_layout is not None:
            qargs = tuple(initial_layout[i] for i in (circuit.find_bit(q).index for q in instruction.qubits)) 
        return qargs

    output_qubit_list = getattr(layout, "_output_qubit_list", None)
    if output_qubit_list is None:
        return qargs

    qargs = tuple(output_qubit_list.index(q) for q in instruction.qubits)

    if DEBUG_PRINT >= 3:
        print(f"[physical qargs] {qargs}", file=sys.stderr)

    return qargs


def estimate_fidelity_from_target(
    transpiled_qc: QuantumCircuit,
    backend: Any,
    initial_layout: list[int] | None = None
) -> dict[str, Any]:
    """
    Estimate circuit fidelity from backend.target InstructionProperties.error.

    The estimate assumes independent instruction errors:

        fidelity ~= product_i (1 - error_i)

    This is only a simple gate-error estimate, not a measured end-to-end
    circuit fidelity.  Measurements are included if the Target supplies a
    measurement error for them.

    If any executable instruction has no usable error value, a complete
    estimate is not reported.
    """
    target = getattr(backend, "target", None)
    if target is None:
        return {
            "estimated_fidelity": None,
            "estimated_error": None,
            "fidelity_estimation_method": None,
            "fidelity_estimation_complete": False,
            "operations_with_error_data": 0,
            "operations_without_error_data": len(transpiled_qc.data),
        }

    log_fidelity = 0.0
    with_error = 0
    without_error = 0
    invalid_error = 0

    # Operations that do not correspond to physical device execution should
    # not prevent an otherwise valid estimate.
    ignored_names = {"barrier", "delay"}

    noLayoutExist = False
    for inst in transpiled_qc.data:
        name = inst.operation.name
        if name in ignored_names:
            continue

        if DEBUG_PRINT >= 2:
            print(f"[Logical instruction] {inst}", file=sys.stderr)
        qargs = _instruction_qubit_indices(transpiled_qc, inst, initial_layout)

        if qargs is None or qargs == ():
            noLayoutExist = True
            continue

        try:
            properties = target[name][qargs]
        except (KeyError, TypeError, AttributeError):
            properties = None
            if( name == "cz" and len(qargs) == 2):
                # Try the reverse qubit order for CZ, which is symmetric.
                try:
                    properties = target[name][qargs[::-1]]
                except (KeyError, TypeError, AttributeError):
                    properties = None

        if DEBUG_PRINT >= 3:
            print(f"[Properties] {name}: {properties}", file=sys.stderr)
        error = getattr(properties, "error", None) if properties is not None else None

        if error is None:
            without_error += 1
            if DEBUG_PRINT >= 2:
                print(f"[Error not provided] {name} {qargs}", file=sys.stderr)
            continue

        try:
            error = float(error)
        except (TypeError, ValueError):
            invalid_error += 1
            if DEBUG_PRINT >= 2:
                print(f"[Invalid error] {name} {qargs}", file=sys.stderr)
            continue

        if not math.isfinite(error) or error < 0.0 or error > 1.0:
            invalid_error += 1
            if DEBUG_PRINT >= 2:
                print(f"[Out of bounds error] {name} {qargs}", file=sys.stderr)
            continue

        with_error += 1
        if DEBUG_PRINT >= 2:
            print(f"[With error] {name} {qargs}", file=sys.stderr)

        if error == 1.0:
            log_fidelity = -math.inf
        elif log_fidelity != -math.inf:
            # Accumulate in log space to avoid unnecessary underflow.
            log_fidelity += math.log1p(-error)

    if noLayoutExist:
        return {
            "estimated_fidelity": None,
            "estimated_error": None,
            "fidelity_estimation_method": None,
            "fidelity_estimation_complete": False,
            "operations_with_error_data": 0,
            "operations_without_error_data": len(transpiled_qc.data),
        }

    complete = (invalid_error == 0 and without_error == 0 and with_error > 0)

    if (invalid_error == 0 and with_error > 0):
        fidelity = 0.0 if log_fidelity == -math.inf else math.exp(log_fidelity)
        estimated_error = 1.0 - fidelity
    else:
        fidelity = None
        estimated_error = None

    return {
        "estimated_fidelity": fidelity,
        "estimated_error": estimated_error,
        "fidelity_estimation_method": (
            "product_of_independent_instruction_fidelities"
            if (invalid_error == 0 and with_error > 0)
            else None
        ),
        "fidelity_estimation_complete": complete,
        "operations_with_error_data": with_error,
        "operations_without_error_data": without_error,
    }


def make_metrics(
    transpiled_qc: QuantumCircuit,
    backend: Any,
    serialized_instructions: list[dict[str, Any]],
    initial_layout: list[int] | None = None,
) -> dict[str, Any]:
    metrics: dict[str, Any] = {
        "physical_circuit_depth": transpiled_qc.depth(),
    }

    metrics.update(count_serialized_operations(serialized_instructions))
    metrics.update(estimate_fidelity_from_target(transpiled_qc, backend, initial_layout))

    # Helpful extra detail for diagnosing the counts.
    metrics["operation_counts_by_name"] = dict(
        sorted(
            Counter(
                str(ins["name"]).lower()
                for ins in serialized_instructions
            ).items()
        )
    )

    return metrics


def main() -> int:
    args = parse_args()
    input_path = args.input_circuit

    if not input_path.is_file():
        print(f"error: input file does not exist: {input_path}", file=sys.stderr)
        return 2

    output_path = output_path_for(input_path)
    submit_input = "-transpiled" in input_path.name and args.shots > 0

    try:
        with input_path.open("r", encoding="utf-8") as f:
            input_circuit = json.load(f)

        if not isinstance(input_circuit, dict):
            raise ValueError("The input JSON must be a circuit object")

        if submit_input and "circuit" in input_circuit:
            input_circuit = input_circuit["circuit"]
            if not isinstance(input_circuit, dict):
                raise ValueError("'circuit' must be a circuit object")

        url = args.base_url or os.environ.get(
            "IQM_BASE_URL", "https://qccsw.ccs.ornl.gov"
        )
        token = args.token
        alias = args.qc_alias or os.environ.get("IQM_QC_ALIAS", "default")

        qc, initial_layout, key_to_cbit = build_quantum_circuit(input_circuit)

        backend = get_backend(url, token, alias)
        if submit_input:
            # Reconstruction compacts the qubits; retain their input hardware names.
            qubit_index_to_name = {
                i: f"QB{hardware_index + 1}"
                for i, hardware_index in enumerate(initial_layout)
            }
            raw_counts = backend.run(
                qc, shots=args.shots, qubit_index_to_name=qubit_index_to_name
            ).result().get_counts()
            if not isinstance(raw_counts, dict):
                raise ValueError("Backend returned multiple classical-register counts")
            counts: dict[str, int] = {
                str(outcome): int(frequency)
                for outcome, frequency in raw_counts.items()
            }
            write_measurement_results(input_path, counts, key_to_cbit)
            if DEBUG_PRINT >= 0:
                print(f"Backend counts: {counts}")
                print(f"Key to cbit mapping: {key_to_cbit}")
            if DEBUG_PRINT >= 0:
                serialized = backend.serialize_circuit(qc)
                serialized_instructions = [
                    serialized_instruction_dict(op)
                    for op in serialized.instructions
                ]
                metrics = make_metrics(
                    qc,
                    backend,
                    serialized_instructions,
                    initial_layout,
                )
                print(f"metrics: {metrics}")
            return 0

        transpiled_qc = transpile_to_IQM(qc, backend, initial_layout=initial_layout)
        if DEBUG_PRINT >= 1:
            print(f"Circuit layout: {transpiled_qc.layout}")
        if args.shots > 0:
            raw_counts = backend.run(
                transpiled_qc, shots=args.shots
            ).result().get_counts()
            if not isinstance(raw_counts, dict):
                raise ValueError("Backend returned multiple classical-register counts")
            counts: dict[str, int] = {
                str(outcome): int(frequency)
                for outcome, frequency in raw_counts.items()
            }
            write_measurement_results(input_path, counts, key_to_cbit)

            if DEBUG_PRINT >= 0:
                print(f"Backend counts: {counts}")
                print(f"Key to cbit mapping: {key_to_cbit}")
        serialized = backend.serialize_circuit(transpiled_qc)

        serialized_instructions = [
            serialized_instruction_dict(op)
            for op in serialized.instructions
        ]

        output_circuit = {
            "name": input_circuit.get("name", "circuit"),
            "instructions": serialized_instructions,
        }
        output = {
            "circuit": output_circuit,
            "url": url,
            "token": token,
            "alias": alias,
            "metrics": make_metrics(
                transpiled_qc,
                backend,
                serialized_instructions,
            ),
        }

        with output_path.open("w", encoding="utf-8") as f:
            json.dump(output, f, indent=2)
            f.write("\n")

        print(f"Wrote transpiled IQM circuit to: {output_path}")
        return 0

    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
