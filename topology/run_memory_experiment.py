#!/usr/bin/env python3

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
from iqm.qiskit_iqm.qiskit_to_iqm import serialize_instructions
from pymatching import Matching
import numpy as np
import stim


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
    parser.add_argument(
        "--simulator",
        action="store_true",
        help="Use a local simulator instead of the IQM API.",
    )
    return parser.parse_args()


def output_path_for(input_path: Path, suffix: str = "transpiled") -> Path:
    """Add a suffix before the existing file extension."""
    return input_path.with_name(
        f"{input_path.stem}-{suffix}{input_path.suffix}"
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


def build_quantum_circuit(circuit: dict[str, Any]) -> tuple[QuantumCircuit, list[int], dict[str, int]]:
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
    measure_map = {}

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
            measure_map[args["key"]] = classical_bit
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

    return qc, initial_layout, measure_map


def get_backend(url: str, token: str | None, alias: str, simulator: bool):
    if simulator:
        match alias:
            case "garnet" | "default":
                from iqm.qiskit_iqm.fake_backends.fake_garnet import IQMFakeGarnet
                return IQMFakeGarnet()
            case "aphrodite":
                from iqm.qiskit_iqm.fake_backends.fake_aphrodite import IQMFakeAphrodite
                return IQMFakeAphrodite()
            case _:
                raise ValueError(f"Unsupported simulator alias {alias!r}.")

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
    return tuple(circuit.find_bit(q).index for q in instruction.qubits)


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

    # Operations that do not correspond to physical device execution should
    # not prevent an otherwise valid estimate.
    ignored_names = {"barrier", "delay"}

    for inst in transpiled_qc.data:
        name = inst.operation.name
        if name in ignored_names:
            continue

        qargs = _instruction_qubit_indices(transpiled_qc, inst)

        try:
            properties = target[name][qargs]
        except (KeyError, TypeError, AttributeError):
            properties = None

        error = getattr(properties, "error", None) if properties is not None else None

        if error is None:
            without_error += 1
            continue

        try:
            error = float(error)
        except (TypeError, ValueError):
            without_error += 1
            continue

        if not math.isfinite(error) or error < 0.0 or error > 1.0:
            without_error += 1
            continue

        with_error += 1

        if error == 1.0:
            log_fidelity = -math.inf
        elif log_fidelity != -math.inf:
            # Accumulate in log space to avoid unnecessary underflow.
            log_fidelity += math.log1p(-error)

    complete = (without_error == 0 and with_error > 0)

    if complete:
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
            if complete
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


cliffords = {
    "I":          np.array([[+1   ,  0   ], [ 0   , +1   ]], dtype=complex),
    "X":          np.array([[ 0   , +1   ], [+1   ,  0   ]], dtype=complex),
    "Y":          np.array([[ 0   ,   -1j], [  +1j,  0   ]], dtype=complex),
    "Z":          np.array([[+1   ,  0   ], [ 0   , -1   ]], dtype=complex),
    "C_NXYZ":     np.array([[+1+1j, +1-1j], [-1-1j, +1-1j]], dtype=complex) / 2,
    "C_NZYX":     np.array([[+1+1j, -1-1j], [+1-1j, +1-1j]], dtype=complex) / 2,
    "C_XNYZ":     np.array([[+1+1j, -1+1j], [+1+1j, +1-1j]], dtype=complex) / 2,
    "C_XYNZ":     np.array([[+1-1j, +1+1j], [-1+1j, +1+1j]], dtype=complex) / 2,
    "C_XYZ":      np.array([[+1-1j, -1-1j], [+1-1j, +1+1j]], dtype=complex) / 2,
    "C_ZNYX":     np.array([[+1-1j, +1-1j], [-1-1j, +1+1j]], dtype=complex) / 2,
    "C_ZYNX":     np.array([[+1-1j, -1+1j], [+1+1j, +1+1j]], dtype=complex) / 2,
    "C_ZYX":      np.array([[+1+1j, +1+1j], [-1+1j, +1-1j]], dtype=complex) / 2,
    "H":          np.array([[+1   , +1   ], [+1   , -1   ]], dtype=complex) / np.sqrt(2),
    "H_NXY":      np.array([[ 0   , +1+1j], [+1-1j,  0   ]], dtype=complex) / np.sqrt(2),
    "H_NXZ":      np.array([[-1   , +1   ], [+1   , +1   ]], dtype=complex) / np.sqrt(2),
    "H_NYZ":      np.array([[-1   ,   -1j], [  +1j, +1   ]], dtype=complex) / np.sqrt(2),
    "H_XY":       np.array([[ 0   , +1-1j], [+1+1j,  0   ]], dtype=complex) / np.sqrt(2),
    "H_YZ":       np.array([[+1   ,   -1j], [  +1j, -1   ]], dtype=complex) / np.sqrt(2),
    "S":          np.array([[+1   ,  0   ], [ 0   ,   +1j]], dtype=complex),
    "SQRT_X":     np.array([[+1+1j, +1-1j], [+1-1j, +1+1j]], dtype=complex) / 2,
    "SQRT_X_DAG": np.array([[+1-1j, +1+1j], [+1+1j, +1-1j]], dtype=complex) / 2,
    "SQRT_Y":     np.array([[+1+1j, -1-1j], [+1+1j, +1+1j]], dtype=complex) / 2,
    "SQRT_Y_DAG": np.array([[+1-1j, +1-1j], [-1+1j, +1-1j]], dtype=complex) / 2,
    "S_DAG":      np.array([[+1   ,  0   ], [ 0   ,   -1j]], dtype=complex),
}


def rx(angle):
    sin, cos = np.sin(angle / 2), np.cos(angle / 2)
    return np.array([
        [      cos, -1j * sin],
        [-1j * sin,       cos],
    ])


def rz(phase):
    exp = np.exp(-1j * phase / 2)
    return np.array([
        [exp,            0],
        [  0, np.conj(exp)],
    ])


def build_stim_circuit(
    instructions: list[dict[str, Any]],
    detectors: list[list[str]],
    observables: list[list[str]],
    measure_map: dict[str, int],
) -> stim.Circuit:
    circuit = stim.Circuit()
    num_measure = 0

    for ins in instructions:
        locus = [qubit_index(q) for q in ins["locus"]]
        match ins["name"]:
            case "prx":
                angle, phase = ins["args"]["angle"], ins["args"]["phase"]
                unitary = rz(phase) @ rx(angle) @ rz(-phase)
                for k, v in cliffords.items():
                    phase_diff = np.angle((unitary * np.conj(v))[v.nonzero()][0])
                    if np.max(abs(unitary - v * np.exp(1j * phase_diff))) < 1e-6:
                        circuit.append(k, locus)
                        circuit.append("DEPOLARIZE1", locus, 1e-3)
                        break
                else:
                    raise ValueError(f"Non-Clifford PRX gate with angle {angle} and phase {phase}.")
            case "cz":
                circuit.append("CZ", locus)
                circuit.append("DEPOLARIZE2", locus, 1e-2)
            case "measure":
                circuit.append("M", locus)
                num_measure += 1
            case "reset":
                circuit.append("R", locus)
            case _:
                raise ValueError(f"Unexpected IQM instruction {ins['name']!r}.")

    for det in detectors:
        circuit.append("DETECTOR", [stim.target_rec(measure_map[key] - num_measure) for key in det])

    for i, obs in enumerate(observables):
        circuit.append("OBSERVABLE_INCLUDE", [stim.target_rec(measure_map[key] - num_measure) for key in obs], i)

    return circuit


def check_stim_circuit(stim_circuit: stim.Circuit, qiskit_circuit: QuantumCircuit):
    from qiskit_aer import AerSimulator
    sim = AerSimulator(method="statevector")

    qiskit_circuit = qiskit_circuit.copy()
    qiskit_circuit.remove_final_measurements()
    qiskit_circuit.save_statevector_dict()
    statevector1 = sim.run(qiskit_circuit).result().data()["statevector"]

    qiskit_circuit = QuantumCircuit.from_qasm_str(stim_circuit.without_noise().decomposed().to_qasm(open_qasm_version=2, skip_dets_and_obs=True))
    qiskit_circuit.remove_final_measurements()
    qiskit_circuit.save_statevector_dict()
    statevector2 = sim.run(qiskit_circuit).result().data()["statevector"]

    assert set(statevector1.keys()) == set(statevector2.keys())
    phase_diff = np.angle(statevector1["0x0"] * np.conj(statevector2["0x0"]))
    assert max(abs(statevector1[k] - statevector2[k] * np.exp(1j * phase_diff)) for k in statevector1.keys()) < 1e-6


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

        qc, initial_layout, measure_map = build_quantum_circuit(input_circuit)

        backend = get_backend(url, token, alias, args.simulator)
        # print(f"Initial layout: {qc.layout}")
        print(f"Initial depth: {qc.depth()}, ops: {qc.count_ops()}")
        transpiled_qc = transpile_to_IQM(qc, backend, initial_layout=initial_layout, optimization_level=1)
        # print(f"Final layout: {transpiled_qc.layout}")
        print(f"Final depth: {transpiled_qc.depth()}, ops: {transpiled_qc.count_ops()}")
        serialized_instructions = [
            serialized_instruction_dict(op)
            for op in serialize_instructions(transpiled_qc, {i: s for i, s in enumerate(backend.architecture.qubits)})
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

        # qasm_path = input_path.with_name(f"{input_path.stem}.qasm")
        # from qiskit import qasm3
        # with qasm_path.open("w", encoding="utf-8") as f:
        #     qasm3.dump(transpiled_qc, f)
        # print(f"Wrote transpiled circuit to: {qasm_path}")

        stim_circuit = build_stim_circuit(
            serialized_instructions,
            input_circuit["detectors"],
            input_circuit["observables"],
            measure_map,
        )
        # check_stim_circuit(stim_circuit, transpiled_qc)

        job = backend.run(transpiled_qc)
        print(f"Job ID: {job.job_id()}")
        job.wait_for_final_state()
        print(f"Job status: {job.status()}")
        counts = job.result().get_counts()
        shots = sum(counts.values())

        counts_path = output_path_for(input_path, "counts")
        with counts_path.open("w", encoding="utf-8") as f:
            json.dump(counts, f, indent=2)
            f.write("\n")
        print(f"Wrote counts to: {counts_path}")

        def check_parity(results, check_name):
            checks = input_circuit[check_name]
            for check in checks:
                parity = 0
                for key in check:
                    parity ^= results[-measure_map[key] - 1] == "1"
                if parity == 1:
                    return False
            return True

        filtered = {k: v for k, v in counts.items() if check_parity(k, "detectors")}
        successful = {k: v for k, v in filtered.items() if check_parity(k, "observables")}
        print(f"Post-selection rate: {sum(filtered.values()) / shots}")
        print(f"Overall success rate with post-selection: {sum(successful.values()) / shots}")
        if filtered:
            print(f"Success rate among post-selected shots: {sum(successful.values()) / sum(filtered.values())}")

        def extract_parities(results, check_name):
            checks = input_circuit[check_name]
            for check in checks:
                parity = 0
                for key in check:
                    parity ^= results[-measure_map[key] - 1] == "1"
                yield parity

        dem = stim_circuit.detector_error_model(decompose_errors=True)
        matching = Matching.from_detector_error_model(dem)

        syndromes = np.array([list(extract_parities(k, "detectors")) for k in counts])
        observables = np.array([list(extract_parities(k, "observables")) for k in counts])
        predictions = matching.decode_batch(syndromes)
        num_errors = np.sum(np.any(predictions != observables, axis=1) * list(counts.values()))
        print(f"Logical error rate with correction: {num_errors / shots}")

        return 0

    except AssertionError:
        raise

    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
