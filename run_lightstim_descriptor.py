#!/usr/bin/env python3
"""Consume a small FTQC-to-LightStim experiment descriptor and print an LER."""

from __future__ import annotations

import argparse
import contextlib
import io
import json
from pathlib import Path
import sys
from typing import Any


LIGHTSTIM_REPO = Path(__file__).resolve().parents[1] / "lightstim"
if str(LIGHTSTIM_REPO) not in sys.path:
    sys.path.insert(0, str(LIGHTSTIM_REPO))

from lightstim.noise.config import NoiseConfig
from lightstim.protocols.memory import MemoryExperiment
from lightstim.protocols.two_patch_ls import TwoPatchLSExperiment
from lightstim.qec_code.HGP import HGPCode, HGPCodeExtractionBlock
from lightstim.simulation.decoder_backend import DecoderConfig, SimulationPipeline


SUPPORTED_FORMAT = "ftqc-lightstim-experiment"
SUPPORTED_VERSION = 1


class DescriptorError(ValueError):
    pass


def require_keys(obj: dict[str, Any], keys: list[str], where: str) -> None:
    missing = [key for key in keys if key not in obj]
    if missing:
        raise DescriptorError(f"{where} missing required keys: {', '.join(missing)}")


def load_descriptor(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        descriptor = json.load(handle)
    validate_descriptor(descriptor)
    return descriptor


def validate_descriptor(descriptor: dict[str, Any]) -> None:
    require_keys(
        descriptor,
        ["format", "version", "experiment", "patches", "operations", "noise", "decoder", "simulation"],
        "descriptor",
    )
    if descriptor["format"] != SUPPORTED_FORMAT:
        raise DescriptorError(f"unsupported format: {descriptor['format']!r}")
    if descriptor["version"] != SUPPORTED_VERSION:
        raise DescriptorError(f"unsupported descriptor version: {descriptor['version']!r}")

    experiment = descriptor["experiment"]
    require_keys(experiment, ["name", "type"], "experiment")
    if experiment["type"] not in ("two_patch_lattice_surgery", "memory"):
        raise DescriptorError(
            "experiment.type must be 'two_patch_lattice_surgery' or 'memory'"
        )

    patches = descriptor["patches"]
    operations = descriptor["operations"]

    if experiment["type"] == "two_patch_lattice_surgery":
        validate_lattice_surgery(patches, operations)
    else:
        validate_memory(patches, operations)

    noise = descriptor["noise"]
    require_keys(noise, ["model", "rates"], "noise")
    if noise["model"] != "circuit_level":
        raise DescriptorError("only noise.model='circuit_level' is currently supported")

    decoder = descriptor["decoder"]
    require_keys(decoder, ["name"], "decoder")
    if decoder["name"] != "pymatching":
        raise DescriptorError("only decoder.name='pymatching' is currently supported")


def validate_patch_common(patch: dict[str, Any], index: int) -> None:
    require_keys(patch, ["id", "code", "distance", "offset", "initial_basis", "measurement_basis"], f"patches[{index}]")
    if patch["initial_basis"] not in ("X", "Z"):
        raise DescriptorError(f"patches[{index}].initial_basis must be X or Z")
    if patch["measurement_basis"] not in ("X", "Z"):
        raise DescriptorError(f"patches[{index}].measurement_basis must be X or Z")
    if len(patch["offset"]) != 2:
        raise DescriptorError(f"patches[{index}].offset must contain two numbers")


def validate_lattice_surgery(
    patches: list[dict[str, Any]],
    operations: list[dict[str, Any]],
) -> None:
    if len(patches) != 2:
        raise DescriptorError("two_patch_lattice_surgery requires exactly two patches")
    for index, patch in enumerate(patches):
        validate_patch_common(patch, index)
        if patch["code"] != "unrotated_surface_code":
            raise DescriptorError("only code='unrotated_surface_code' is currently supported for lattice surgery")

    if len(operations) != 1:
        raise DescriptorError("two_patch_lattice_surgery currently supports exactly one operation")
    operation = operations[0]
    require_keys(operation, ["type", "interaction", "patches", "rounds"], "operations[0]")
    if operation["type"] != "lattice_surgery":
        raise DescriptorError("operation.type must be 'lattice_surgery'")
    if operation["interaction"] not in ("ZZ", "XX"):
        raise DescriptorError("operation.interaction must be ZZ or XX")
    if operation["patches"] != [patches[0]["id"], patches[1]["id"]]:
        raise DescriptorError("operation.patches must reference the two patches in descriptor order")


def validate_memory(
    patches: list[dict[str, Any]],
    operations: list[dict[str, Any]],
) -> None:
    if len(patches) != 1:
        raise DescriptorError("memory requires exactly one patch")

    patch = patches[0]
    validate_patch_common(patch, 0)
    if patch["initial_basis"] != patch["measurement_basis"]:
        raise DescriptorError("patches[0].initial_basis must equal patches[0].measurement_basis for memory experiments")
    if patch["code"] != "hypergraph_product":
        raise DescriptorError("only code='hypergraph_product' is currently supported for memory experiments")
    require_keys(patch, ["classical_codes"], "patches[0]")
    classical_codes = patch["classical_codes"]
    require_keys(classical_codes, ["code1", "code2"], "patches[0].classical_codes")
    validate_classical_code(classical_codes["code1"], "patches[0].classical_codes.code1")
    validate_classical_code(classical_codes["code2"], "patches[0].classical_codes.code2")

    for index, operation in enumerate(operations):
        require_keys(operation, ["type", "patch", "rounds"], f"operations[{index}]")
        if operation["type"] != "syndrome_extraction":
            raise DescriptorError(f"operations[{index}].type must be 'syndrome_extraction'")
        if operation["patch"] != patch["id"]:
            raise DescriptorError(f"operations[{index}].patch must reference {patch['id']!r}")
        if int(operation["rounds"]) <= 0:
            raise DescriptorError(f"operations[{index}].rounds must be positive")


def validate_classical_code(code: Any, where: str) -> None:
    if not isinstance(code, list):
        raise DescriptorError(f"{where} must be a list of rows")
    for row_index, row in enumerate(code):
        if not isinstance(row, list):
            raise DescriptorError(f"{where}[{row_index}] must be a list")
        for bit in row:
            if bit not in (0, 1):
                raise DescriptorError(f"{where}[{row_index}] contains out-of-range bit {bit}")


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


def build_circuit(descriptor: dict[str, Any], noisy: bool = True):
    experiment_type = descriptor["experiment"]["type"]
    if experiment_type == "two_patch_lattice_surgery":
        return build_lattice_surgery_circuit(descriptor, noisy=noisy)
    if experiment_type == "memory":
        return build_memory_circuit(descriptor, noisy=noisy)
    raise DescriptorError(f"unsupported experiment type: {experiment_type!r}")


def build_lattice_surgery_circuit(descriptor: dict[str, Any], noisy: bool = True):
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


def build_memory_circuit(descriptor: dict[str, Any], noisy: bool = True):
    patch_desc = descriptor["patches"][0]
    patch = HGPCode(
        patch_desc["classical_codes"]["code1"],
        patch_desc["classical_codes"]["code2"],
        d=patch_desc["distance"]
    )
    experiment = MemoryExperiment(
        qec_patch=patch,
        patch_name=patch_desc["id"],
        extraction_block_class=HGPCodeExtractionBlock,
        rounds=sum(int(op["rounds"]) for op in descriptor["operations"]),
        basis=patch_desc["initial_basis"],
        noise_params=noise_config(descriptor) if noisy else None,
        noise_model=descriptor["noise"]["model"],
    )
    return experiment.build()


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


def main() -> None:
    args = parse_args()
    descriptor_path = Path(args.descriptor)
    if not descriptor_path.exists() and not descriptor_path.is_absolute():
        descriptor_path = Path(__file__).resolve().parent / descriptor_path
    descriptor = load_descriptor(descriptor_path)
    if not args.skip_noiseless_check:
        check_noiseless_circuit(descriptor)
    circuit, stats = run_simulation(descriptor, progress=args.progress)

    experiment = descriptor["experiment"]
    patches = descriptor["patches"]
    rates = descriptor["noise"]["rates"]

    print(f"experiment: {experiment['name']}")
    if experiment["type"] == "two_patch_lattice_surgery":
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
        rounds = sum(int(op["rounds"]) for op in descriptor["operations"])
        patch = patches[0]
        print(f"operation: memory experiment, syndrome_rounds={rounds}")
        print(
            "patches: "
            f"{patch['id']} code={patch['code']} d={patch['distance']} offset={patch['offset']} "
            f"init={patch['initial_basis']} meas={patch['measurement_basis']}"
        )
    print(f"circuit: qubits={circuit.num_qubits} detectors={circuit.num_detectors} observables={circuit.num_observables}")
    print(f"noise: {descriptor['noise']['model']} {rates}")
    print(f"decoder: {descriptor['decoder']['name']}")
    print(f"shots: {stats.shots}")
    print(f"post-selected shots: {stats.post_selected_shots}")
    print(f"logical errors: {stats.errors}")
    print(f"LER: {stats.logical_error_rate:.6e} +/- {stats.ler_error_bar():.2e} (95% normal approx.)")


if __name__ == "__main__":
    main()
