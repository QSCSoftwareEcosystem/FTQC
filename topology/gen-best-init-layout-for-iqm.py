#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Select the best logical-to-hardware initial qubit layout for an "
            "IQM machine using unavailable-qubit information and per-qubit "
            "prx_error values."
        )
    )

    parser.add_argument(
        "--qec-name",
        required=True,
        help='QEC code name, for example "steane", "surface", or "color_code".',
    )
    parser.add_argument(
        "--num-qubits",
        required=True,
        type=int,
        help="Number of logical qubits to map.",
    )
    parser.add_argument(
        "--num-anc-per-logic-qubit",
        required=True,
        type=int,
        help="Number of ancilla qubits used per logical qubit.",
    )
    parser.add_argument(
        "--input-init-layouts",
        required=True,
        type=Path,
        help="JSON file containing candidate initial layouts.",
    )
    parser.add_argument(
        "--hw-topology",
        required=True,
        type=Path,
        help="JSON file describing the target IQM hardware topology.",
    )
    parser.add_argument(
        "--output-init-layout",
        required=True,
        type=Path,
        help="Output JSON file for the selected initial layout.",
    )

    return parser.parse_args()


def load_json(path: Path, description: str) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except FileNotFoundError:
        raise RuntimeError(f"{description} file does not exist: {path}")
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Failed to parse {description} JSON file '{path}': {exc}"
        )

    if not isinstance(data, dict):
        raise RuntimeError(
            f"{description} JSON file '{path}' must contain a top-level JSON object."
        )

    return data


def hardware_qubit_name(hardware_qubit_id: int) -> str:
    """
    Convert a zero-based hardware qubit ID to the IQM qubit name.

    Example:
        19 -> QB20
        6  -> QB7
    """
    return f"QB{hardware_qubit_id + 1}"


def get_candidate_layouts(
    init_layouts: dict[str, Any],
    qec_name: str,
    num_ancilla: int,
) -> list[list[int]]:
    if qec_name not in init_layouts:
        raise RuntimeError(
            f'QEC code "{qec_name}" was not found in the initial-layout JSON file.'
        )

    qec_entry = init_layouts[qec_name]
    if not isinstance(qec_entry, dict):
        raise RuntimeError(
            f'Entry "{qec_name}" in the initial-layout JSON file must be an object.'
        )

    ancilla_key = f"{num_ancilla}ancilla"
    if ancilla_key not in qec_entry:
        raise RuntimeError(
            f'Ancilla configuration "{ancilla_key}" was not found under '
            f'QEC code "{qec_name}".'
        )

    candidates = qec_entry[ancilla_key]
    if not isinstance(candidates, list) or not candidates:
        raise RuntimeError(
            f'Entry "{qec_name}/{ancilla_key}" must contain a non-empty list '
            "of candidate layouts."
        )

    return candidates


def validate_candidate_prefix(
    candidate: Any,
    candidate_index: int,
    num_qubits: int,
    hardware_num_qubits: int,
) -> list[int]:
    if not isinstance(candidate, list):
        raise RuntimeError(
            f"Candidate layout #{candidate_index} is not a JSON list."
        )

    if len(candidate) < num_qubits:
        raise RuntimeError(
            f"Candidate layout #{candidate_index} contains only {len(candidate)} "
            f"entries, but --num-qubits={num_qubits} requires at least "
            f"{num_qubits} entries."
        )

    selected = candidate[:num_qubits]

    for logical_id, hardware_id in enumerate(selected):
        if not isinstance(hardware_id, int) or isinstance(hardware_id, bool):
            raise RuntimeError(
                f"Candidate layout #{candidate_index}, logical qubit "
                f"{logical_id}: hardware qubit ID must be an integer."
            )

        if hardware_id < 0 or hardware_id >= hardware_num_qubits:
            raise RuntimeError(
                f"Candidate layout #{candidate_index}, logical qubit "
                f"{logical_id}: hardware qubit ID {hardware_id} is outside "
                f"the valid range 0..{hardware_num_qubits - 1}."
            )

    if len(set(selected)) != len(selected):
        raise RuntimeError(
            f"Candidate layout #{candidate_index} maps multiple logical qubits "
            "to the same hardware qubit."
        )

    return selected


def calculate_prx_error_sum(
    layout: list[int],
    neighbours: dict[str, Any],
) -> float:
    total_error = 0.0

    for hardware_id in layout:
        qubit_name = hardware_qubit_name(hardware_id)

        if qubit_name not in neighbours:
            raise RuntimeError(
                f'Hardware topology is missing "{qubit_name}" in "neighbours".'
            )

        qubit_info = neighbours[qubit_name]
        if not isinstance(qubit_info, dict):
            raise RuntimeError(
                f'Hardware topology entry "neighbours/{qubit_name}" must be an object.'
            )

        if "prx_error" not in qubit_info:
            raise RuntimeError(
                f'Hardware topology entry "neighbours/{qubit_name}" '
                'does not contain "prx_error".'
            )

        prx_error = qubit_info["prx_error"]
        if not isinstance(prx_error, (int, float)) or isinstance(prx_error, bool):
            raise RuntimeError(
                f'Hardware topology entry "neighbours/{qubit_name}/prx_error" '
                "must be numeric."
            )

        total_error += float(prx_error)

    return total_error


def find_best_layout(
    candidates: list[list[int]],
    num_qubits: int,
    hw_topology: dict[str, Any],
) -> tuple[list[int], float, int]:
    if "num_qubits" not in hw_topology:
        raise RuntimeError('Hardware topology JSON is missing "num_qubits".')

    hardware_num_qubits = hw_topology["num_qubits"]
    if not isinstance(hardware_num_qubits, int) or isinstance(
        hardware_num_qubits, bool
    ):
        raise RuntimeError(
            'Hardware topology value "num_qubits" must be an integer.'
        )

    if num_qubits <= 0:
        raise RuntimeError("--num-qubits must be greater than zero.")

    if num_qubits > hardware_num_qubits:
        raise RuntimeError(
            f"No valid initial layout exists: requested {num_qubits} qubits, "
            f"but the target hardware contains only {hardware_num_qubits} qubits."
        )

    unavailable_qubits = hw_topology.get("unavailable_qubits", [])
    if not isinstance(unavailable_qubits, list):
        raise RuntimeError(
            'Hardware topology value "unavailable_qubits" must be a list.'
        )
    unavailable_set = set(unavailable_qubits)

    neighbours = hw_topology.get("neighbours")
    if not isinstance(neighbours, dict):
        raise RuntimeError(
            'Hardware topology JSON must contain a "neighbours" object.'
        )

    best_layout = None
    best_error_sum = None
    best_candidate_index = None

    for candidate_index, candidate in enumerate(candidates):
        layout = validate_candidate_prefix(
            candidate,
            candidate_index,
            num_qubits,
            hardware_num_qubits,
        )

        mapped_qubit_names = [
            hardware_qubit_name(hardware_id) for hardware_id in layout
        ]

        # Reject a candidate if any mapped hardware qubit is unavailable.
        if any(name in unavailable_set for name in mapped_qubit_names):
            continue

        error_sum = calculate_prx_error_sum(layout, neighbours)

        # A strict '<' intentionally keeps the first candidate in case of a tie.
        if best_error_sum is None or error_sum < best_error_sum:
            best_layout = layout
            best_error_sum = error_sum
            best_candidate_index = candidate_index

    if best_layout is None:
        raise RuntimeError(
            "No valid initial layout exists after excluding candidates that "
            "use unavailable hardware qubits."
        )

    return best_layout, best_error_sum, best_candidate_index


def write_output(path: Path, layout: list[int]) -> None:
    output = {
        "logical_to_hardware_mapping": {
            str(logical_id): hardware_id
            for logical_id, hardware_id in enumerate(layout)
        }
    }

    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8") as f:
            json.dump(output, f, indent=4)
            f.write("\n")
    except OSError as exc:
        raise RuntimeError(f"Failed to write output file '{path}': {exc}")


def main() -> int:
    args = parse_args()

    try:
        if args.num_anc_per_logic_qubit < 0:
            raise RuntimeError(
                "--num-anc-per-logic-qubit must be zero or greater."
            )

        init_layouts = load_json(
            args.input_init_layouts, "Initial-layout"
        )
        hw_topology = load_json(
            args.hw_topology, "Hardware-topology"
        )

        candidates = get_candidate_layouts(
            init_layouts,
            args.qec_name,
            args.num_anc_per_logic_qubit,
        )

        best_layout, best_error_sum, candidate_index = find_best_layout(
            candidates,
            args.num_qubits,
            hw_topology,
        )

        write_output(args.output_init_layout, best_layout)

        print(
            f"Selected candidate layout #{candidate_index} "
            f"with total prx_error = {best_error_sum:.17g}"
        )
        print(f"Best layout: {best_layout}")
        print(f"Output written to: {args.output_init_layout}")

        return 0

    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
