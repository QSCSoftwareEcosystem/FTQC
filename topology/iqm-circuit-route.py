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
        --qc-alias [my-quantum-computer]

Output:
    inputCircuit-transpiled.JSON

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

DEBUG_PRINT = 1
_QUBIT_RE = re.compile(r"^QB(\d+)$", re.IGNORECASE)


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
    return parser.parse_args()


def output_path_for(input_path: Path) -> Path:
    """Add '-transpiled' before the existing file extension."""
    return input_path.with_name(
        f"{input_path.stem}-transpiled{input_path.suffix}"
    )


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


def build_quantum_circuit(circuit: dict[str, Any]) -> tuple[QuantumCircuit, list[int]]:
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

    for ins in instructions:
        name = ins.get("name", "").lower()
        args = ins.get("args", {})
        locus = ins.get("locus", [])
        loc = [internal_qubit_mapping[q] for q in locus]

        if name == "prx":
            if len(loc) != 1:
                raise ValueError(f"'prx' requires one qubit: {ins}")
            if not isinstance(args, dict) or not {
                "angle_t", "phase_t"
            }.issubset(args):
                raise ValueError(
                    "'prx' requires numeric 'angle_t' and 'phase_t' "
                    f"arguments in turns: {ins}"
                )
            try:
                angle = float(args["angle_t"]) * math.tau
                phase = float(args["phase_t"]) * math.tau
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    "'prx' requires numeric 'angle_t' and 'phase_t' "
                    f"arguments in turns: {ins}"
                ) from exc
            qc.r(angle, phase, loc[0])

        elif name == "cz":
            if len(loc) != 2:
                raise ValueError(f"'cz' requires two qubits: {ins}")
            qc.cz(loc[0], loc[1])

        elif name == "measure":
            if len(loc) != 1:
                raise ValueError(f"'measure' requires one qubit: {ins}")
            qc.measure(loc[0], classical_bit)
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

    return qc, initial_layout


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


def _instruction_qubit_indices(circuit: QuantumCircuit, instruction: Any) -> tuple[int, ...]:
    """Return the instruction qubit indices in backend hardware coordinates.

    The transpiled circuit may carry a final layout mapping virtual qubit
    indices to physical backend qubits.  Use that mapping when present so
    backend.target lookups use hardware qubit indices instead of the transient
    virtual indices used within the transpiled circuit.
    """
    # circuit.find_bit().index returns the virtual qubit index used in the transpiled QuantumCircuit.
    # qargs = tuple(circuit.find_bit(q).index for q in instruction.qubits)
    qargs = None

    layout = getattr(circuit, "layout", None)
    if layout is None:
        return qargs
	
    output_qubit_list = getattr(layout, "_output_qubit_list", None)
    if output_qubit_list is None:
        return qargs

    qargs = tuple(output_qubit_list.index(q) for q in instruction.qubits)

    if DEBUG_PRINT >= 2:
        print(f"[physical qargs] {qargs}", file=sys.stderr)

    return qargs


def estimate_fidelity_from_target(
    transpiled_qc: QuantumCircuit,
    backend: Any,
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

    for inst in transpiled_qc.data:
        name = inst.operation.name
        if name in ignored_names:
            continue

        if DEBUG_PRINT >= 2:
            print(f"[Logical instruction] {inst}", file=sys.stderr)
        qargs = _instruction_qubit_indices(transpiled_qc, inst)

        if qargs is None:
            return {
                "estimated_fidelity": None,
                "estimated_error": None,
                "fidelity_estimation_method": None,
                "fidelity_estimation_complete": False,
                "operations_with_error_data": 0,
                "operations_without_error_data": len(transpiled_qc.data),
            }

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

        if DEBUG_PRINT >= 2:
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
) -> dict[str, Any]:
    metrics: dict[str, Any] = {
        "physical_circuit_depth": transpiled_qc.depth(),
    }

    metrics.update(count_serialized_operations(serialized_instructions))
    metrics.update(estimate_fidelity_from_target(transpiled_qc, backend))

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

    try:
        with input_path.open("r", encoding="utf-8") as f:
            input_circuit = json.load(f)

        if not isinstance(input_circuit, dict):
            raise ValueError("The input JSON must be a circuit object")

        url = args.base_url or os.environ.get(
            "IQM_BASE_URL", "https://qccsw.ccs.ornl.gov"
        )
        token = args.token
        alias = args.qc_alias or os.environ.get("IQM_QC_ALIAS", "default")

        qc, initial_layout = build_quantum_circuit(input_circuit)

        backend = get_backend(url, token, alias)
        transpiled_qc = transpile_to_IQM(qc, backend, initial_layout=initial_layout)
        if DEBUG_PRINT >= 1:
            print(f"Circuit layout: {transpiled_qc.layout}")
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
