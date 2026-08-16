#!/usr/bin/env python3
"""Run FTQC->LightStim descriptors (legacy v1 and atomic timeline v2)."""

from __future__ import annotations

import argparse
import contextlib
import copy
import io
import json
from pathlib import Path
import sys
from typing import Any


LIGHTSTIM_REPO = Path(__file__).resolve().parents[1] / "lightstim"
if str(LIGHTSTIM_REPO) not in sys.path:
    sys.path.insert(0, str(LIGHTSTIM_REPO))

from lightstim.noise.config import NoiseConfig
from lightstim.protocols.atomic_timeline import AtomicTimelineError, AtomicTimelineExperiment
from lightstim.protocols.two_patch_ls import TwoPatchLSExperiment
from lightstim.simulation.decoder_backend import DecoderConfig, SimulationPipeline


LEGACY_FORMAT = "ftqc-lightstim-experiment"
LEGACY_VERSION = 1
ATOMIC_EXPERIMENT_FORMAT = "ftqc-lightstim-atomic-experiment"
ATOMIC_IR_FORMAT = "ftqc-lightstim-atomic-ir"
ATOMIC_VERSION = 2


class DescriptorError(ValueError):
    pass


def require_keys(obj: dict[str, Any], keys: list[str], where: str) -> None:
    missing = [key for key in keys if key not in obj]
    if missing:
        raise DescriptorError(f"{where} missing required keys: {', '.join(missing)}")


def is_legacy_descriptor(descriptor: dict[str, Any]) -> bool:
    return descriptor.get("format") == LEGACY_FORMAT and int(descriptor.get("version", -1)) == LEGACY_VERSION


def is_atomic_descriptor(descriptor: dict[str, Any]) -> bool:
    return descriptor.get("format") in (ATOMIC_EXPERIMENT_FORMAT, ATOMIC_IR_FORMAT) and int(
        descriptor.get("version", -1)
    ) == ATOMIC_VERSION


def default_runtime_section() -> dict[str, Any]:
    return {
        "noise": {
            "model": "circuit_level",
            "rates": {
                "p_1q": 1e-3,
                "p_2q": 1e-3,
                "p_meas": 1e-3,
                "p_reset": 1e-3,
                "p_idle": 1e-3,
            },
        },
        "decoder": {"name": "pymatching", "backend": "cpu"},
        "simulation": {
            "max_errors": 100,
            "max_shots": 1_000_000,
            "batch_size": 10_000,
            "num_workers": 4,
            "noiseless_check_shots": 100,
        },
    }


def with_runtime_defaults(descriptor: dict[str, Any]) -> dict[str, Any]:
    """Ensure noise/decoder/simulation sections exist.

    Atomic IR descriptors intentionally omit runtime knobs. For execution, we
    inject stable defaults here.
    """
    merged = copy.deepcopy(descriptor)
    defaults = default_runtime_section()

    if "noise" not in merged:
        merged["noise"] = defaults["noise"]
    else:
        merged["noise"].setdefault("model", defaults["noise"]["model"])
        rates = merged["noise"].setdefault("rates", {})
        for key, value in defaults["noise"]["rates"].items():
            rates.setdefault(key, value)

    if "decoder" not in merged:
        merged["decoder"] = defaults["decoder"]
    else:
        merged["decoder"].setdefault("name", defaults["decoder"]["name"])
        merged["decoder"].setdefault("backend", defaults["decoder"]["backend"])

    if "simulation" not in merged:
        merged["simulation"] = defaults["simulation"]
    else:
        for key, value in defaults["simulation"].items():
            merged["simulation"].setdefault(key, value)

    return merged


def load_descriptor(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        descriptor = json.load(handle)
    validate_descriptor(descriptor)
    return with_runtime_defaults(descriptor) if is_atomic_descriptor(descriptor) else descriptor


def validate_legacy_descriptor(descriptor: dict[str, Any]) -> None:
    require_keys(
        descriptor,
        ["format", "version", "experiment", "patches", "operations", "noise", "decoder", "simulation"],
        "descriptor",
    )

    experiment = descriptor["experiment"]
    require_keys(experiment, ["name", "type"], "experiment")
    if experiment["type"] != "two_patch_lattice_surgery":
        raise DescriptorError("only experiment.type='two_patch_lattice_surgery' is currently supported")

    patches = descriptor["patches"]
    if len(patches) != 2:
        raise DescriptorError("two_patch_lattice_surgery requires exactly two patches")
    for index, patch in enumerate(patches):
        require_keys(
            patch,
            ["id", "code", "distance", "offset", "initial_basis", "measurement_basis"],
            f"patches[{index}]",
        )
        if patch["code"] != "unrotated_surface_code":
            raise DescriptorError("only code='unrotated_surface_code' is currently supported")
        if patch["initial_basis"] not in ("X", "Z"):
            raise DescriptorError(f"patches[{index}].initial_basis must be X or Z")
        if patch["measurement_basis"] not in ("X", "Z"):
            raise DescriptorError(f"patches[{index}].measurement_basis must be X or Z")
        if len(patch["offset"]) != 2:
            raise DescriptorError(f"patches[{index}].offset must contain two numbers")

    operations = descriptor["operations"]
    if len(operations) != 1:
        raise DescriptorError("this adapter currently supports exactly one operation")
    operation = operations[0]
    require_keys(operation, ["type", "interaction", "patches", "rounds"], "operations[0]")
    if operation["type"] != "lattice_surgery":
        raise DescriptorError("only operation type='lattice_surgery' is currently supported")
    if operation["interaction"] not in ("ZZ", "XX"):
        raise DescriptorError("operation.interaction must be ZZ or XX")
    if operation["patches"] != [patches[0]["id"], patches[1]["id"]]:
        raise DescriptorError("operation.patches must reference the two patches in descriptor order")


def validate_atomic_descriptor(descriptor: dict[str, Any]) -> None:
    require_keys(descriptor, ["format", "version", "experiment", "patches", "timeline"], "descriptor")

    experiment = descriptor["experiment"]
    require_keys(experiment, ["name", "type"], "experiment")
    if experiment["type"] != "atomic_timeline":
        raise DescriptorError("atomic descriptors require experiment.type='atomic_timeline'")

    patches = descriptor["patches"]
    if not patches:
        raise DescriptorError("atomic descriptor requires at least one patch")
    patch_ids: set[str] = set()
    for index, patch in enumerate(patches):
        require_keys(
            patch,
            ["id", "code", "distance", "offset", "initial_basis", "measurement_basis"],
            f"patches[{index}]",
        )
        patch_id = str(patch["id"])
        if patch_id in patch_ids:
            raise DescriptorError(f"duplicate patch id {patch_id!r}")
        patch_ids.add(patch_id)

        if patch["code"] != "unrotated_surface_code":
            raise DescriptorError("atomic adapter currently supports only code='unrotated_surface_code'")
        if patch["initial_basis"] not in ("X", "Y", "Z"):
            raise DescriptorError(f"patches[{index}].initial_basis must be X/Y/Z")
        if patch["measurement_basis"] not in ("X", "Y", "Z"):
            raise DescriptorError(f"patches[{index}].measurement_basis must be X/Y/Z")
        if len(patch["offset"]) != 2:
            raise DescriptorError(f"patches[{index}].offset must contain two numbers")

    timeline = descriptor["timeline"]
    if not isinstance(timeline, list) or not timeline:
        raise DescriptorError("atomic descriptor.timeline must be a non-empty list")

    for i, entry in enumerate(timeline):
        op = entry.get("op")
        if not op:
            raise DescriptorError(f"timeline[{i}] missing required key 'op'")

        if op == "initialize":
            require_keys(entry, ["patch", "basis"], f"timeline[{i}]")
        elif op == "syndrome_extraction":
            rounds = int(entry.get("rounds", 1))
            if rounds <= 0:
                raise DescriptorError(f"timeline[{i}].rounds must be > 0")
        elif op == "activate_coupler":
            require_keys(entry, ["name", "interaction", "patches"], f"timeline[{i}]")
            if entry["interaction"] not in ("XX", "ZZ"):
                raise DescriptorError(f"timeline[{i}].interaction must be XX or ZZ")
            if len(entry["patches"]) != 2:
                raise DescriptorError(f"timeline[{i}].patches must contain exactly two patch ids")
        elif op == "deactivate_coupler":
            require_keys(entry, ["name"], f"timeline[{i}]")
        elif op == "unitary_block":
            require_keys(entry, ["gates"], f"timeline[{i}]")
        elif op == "data_readout":
            require_keys(entry, ["measurements"], f"timeline[{i}]")
        else:
            raise DescriptorError(f"timeline[{i}] unsupported op {op!r}")


def validate_descriptor(descriptor: dict[str, Any]) -> None:
    if is_legacy_descriptor(descriptor):
        validate_legacy_descriptor(descriptor)
        return
    if is_atomic_descriptor(descriptor):
        validate_atomic_descriptor(descriptor)
        return

    raise DescriptorError(
        "unsupported descriptor format/version: "
        f"format={descriptor.get('format')!r}, version={descriptor.get('version')!r}"
    )


def noise_config(descriptor: dict[str, Any] | None) -> NoiseConfig | None:
    if descriptor is None:
        return None
    rates = descriptor["noise"]["rates"]
    return NoiseConfig(
        p_1q=float(rates.get("p_1q", 0.0)),
        p_2q=float(rates.get("p_2q", 0.0)),
        p_meas=float(rates.get("p_meas", 0.0)),
        p_reset=float(rates.get("p_reset", 0.0)),
        p_idle=float(rates.get("p_idle", 0.0)),
    )


def build_legacy_circuit(descriptor: dict[str, Any], noisy: bool = True):
    patches = descriptor["patches"]
    operation = descriptor["operations"][0]
    patch0, patch1 = patches

    offset0 = tuple(patch0["offset"])
    if offset0 != (0, 0):
        raise DescriptorError("TwoPatchLSExperiment expects the first patch at offset [0, 0]")

    experiment = TwoPatchLSExperiment(
        patch1_config={"distance": int(patch0["distance"])},
        patch2_config={"distance": int(patch1["distance"])},
        offset=tuple(patch1["offset"]),
        interaction_type=operation["interaction"],
        initial_state_patch1=patch0["initial_basis"],
        initial_state_patch2=patch1["initial_basis"],
        measure_state_patch1=patch0["measurement_basis"],
        measure_state_patch2=patch1["measurement_basis"],
        rounds=int(operation["rounds"]),
        noise_params=noise_config(descriptor) if noisy else None,
        noise_model=descriptor["noise"]["model"],
        rotate_patch1=bool(operation.get("rotate_first_patch", False)),
    )

    with contextlib.redirect_stdout(io.StringIO()):
        return experiment.build()


def build_atomic_circuit(descriptor: dict[str, Any], noisy: bool = True):
    experiment = AtomicTimelineExperiment(
        descriptor=descriptor,
        noise_params=noise_config(descriptor) if noisy else None,
        noise_model=descriptor["noise"]["model"],
        if_detector=True,
    )
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            return experiment.build()
    except AtomicTimelineError as exc:
        raise DescriptorError(str(exc)) from exc


def build_circuit(descriptor: dict[str, Any], noisy: bool = True):
    if is_legacy_descriptor(descriptor):
        return build_legacy_circuit(descriptor, noisy=noisy)
    return build_atomic_circuit(descriptor, noisy=noisy)


def check_noiseless_circuit(descriptor: dict[str, Any]) -> None:
    shots = int(descriptor["simulation"].get("noiseless_check_shots", 100))
    if shots <= 0:
        return
    circuit = build_circuit(descriptor, noisy=False)
    sampler = circuit.compile_detector_sampler()
    dets, obs = sampler.sample(shots=shots, separate_observables=True)
    if dets.any() or obs.any():
        raise RuntimeError("Noiseless sanity check failed: detector event or observable flip observed.")


def run_simulation(descriptor: dict[str, Any], progress: bool):
    simulation = descriptor["simulation"]
    decoder = descriptor["decoder"]
    circuit = build_circuit(descriptor, noisy=True)
    pipeline = SimulationPipeline(
        decoder_config=DecoderConfig(decoder["name"], backend=decoder.get("backend", "cpu")),
        max_errors=int(simulation.get("max_errors", 100)),
        max_shots=int(simulation.get("max_shots", 1_000_000)),
        batch_size=int(simulation.get("batch_size", 10_000)),
        num_workers=int(simulation.get("num_workers", 4)),
        print_progress=progress,
    )
    return circuit, pipeline.run(circuit, json_metadata=descriptor)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a LightStim experiment descriptor.")
    parser.add_argument(
        "descriptor",
        nargs="?",
        default="unrotated_zz_ls_descriptor.json",
        help="Path to the descriptor JSON file.",
    )
    parser.add_argument(
        "--progress",
        action="store_true",
        help="Print LightStim simulation progress.",
    )
    parser.add_argument(
        "--skip-noiseless-check",
        action="store_true",
        help="Skip the descriptor's noiseless sanity check.",
    )
    return parser.parse_args()


def print_descriptor_summary(descriptor: dict[str, Any], circuit: Any) -> None:
    experiment = descriptor["experiment"]
    rates = descriptor["noise"]["rates"]

    print(f"experiment: {experiment['name']}")

    if is_legacy_descriptor(descriptor):
        patches = descriptor["patches"]
        operation = descriptor["operations"][0]
        print(f"operation: {operation['interaction']} lattice surgery, rounds={operation['rounds']}")
        print(
            "patches: "
            f"{patches[0]['id']} d={patches[0]['distance']} offset={patches[0]['offset']} "
            f"init={patches[0]['initial_basis']} meas={patches[0]['measurement_basis']}; "
            f"{patches[1]['id']} d={patches[1]['distance']} offset={patches[1]['offset']} "
            f"init={patches[1]['initial_basis']} meas={patches[1]['measurement_basis']}"
        )
    else:
        timeline = descriptor["timeline"]
        print(f"operation model: atomic timeline (steps={len(timeline)})")
        print(f"patch count: {len(descriptor['patches'])}")

    print(
        f"circuit: qubits={circuit.num_qubits} "
        f"detectors={circuit.num_detectors} observables={circuit.num_observables}"
    )
    print(f"noise: {descriptor['noise']['model']} {rates}")
    print(f"decoder: {descriptor['decoder']['name']}[{descriptor['decoder'].get('backend', 'cpu')}]")


def main() -> None:
    args = parse_args()
    descriptor_path = Path(args.descriptor)
    if not descriptor_path.exists() and not descriptor_path.is_absolute():
        descriptor_path = Path(__file__).resolve().parent / descriptor_path

    descriptor = load_descriptor(descriptor_path)
    if not args.skip_noiseless_check:
        check_noiseless_circuit(descriptor)
    circuit, stats = run_simulation(descriptor, progress=args.progress)

    print_descriptor_summary(descriptor, circuit)
    print(f"shots: {stats.shots}")
    print(f"post-selected shots: {stats.post_selected_shots}")
    print(f"logical errors: {stats.errors}")
    print(f"LER: {stats.logical_error_rate:.6e} +/- {stats.ler_error_bar():.2e} (95% normal approx.)")


if __name__ == "__main__":
    main()
