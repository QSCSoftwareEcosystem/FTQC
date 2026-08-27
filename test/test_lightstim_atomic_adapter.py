# RUN: %lightstim_python %s
# REQUIRES: lightstim-python

"""Regression tests for the FTQC atomic-timeline LightStim adapter."""

from __future__ import annotations

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    from lightstim_atomic_adapter import AtomicTimelineError, AtomicTimelineExperiment
except ModuleNotFoundError as exc:
    if exc.name not in {"stim", "lightstim"}:
        raise
    _LIGHTSTIM_IMPORT_ERROR = exc
else:
    _LIGHTSTIM_IMPORT_ERROR = None


def _descriptor() -> dict:
    return {
        "format": "ftqc-lightstim-atomic-ir",
        "version": 2,
        "experiment": {"name": "atomic_adapter_test", "type": "atomic_timeline"},
        "patches": [
            {
                "id": "left",
                "code": "unrotated_surface_code",
                "distance": 3,
                "offset": [0, 0],
                "initial_basis": "X",
                "measurement_basis": "X",
            },
            {
                "id": "right",
                "code": "unrotated_surface_code",
                "distance": 5,
                "offset": [0, 10],
                "initial_basis": "Z",
                "measurement_basis": "Z",
            },
        ],
        "timeline": [
            {"op": "initialize", "patch": "left", "basis": "X"},
            {"op": "initialize", "patch": "right", "basis": "Z"},
            {
                "op": "unitary_block",
                "gates": [{"gate": "logical_h", "patches": ["left"]}],
            },
            {"op": "syndrome_extraction", "rounds": 3},
            {
                "op": "activate_coupler",
                "name": "bridge",
                "interaction": "ZZ",
                "patches": ["left", "right"],
            },
            {"op": "syndrome_extraction", "rounds": 2, "optimize_repeat": True},
            {"op": "deactivate_coupler", "name": "bridge"},
            {
                "op": "data_readout",
                "measurements": [
                    {"patch": "left", "basis": "X"},
                    {"patch": "right", "basis": "Z"},
                ],
            },
        ],
    }


@unittest.skipIf(
    _LIGHTSTIM_IMPORT_ERROR is not None,
    f"LightStim runtime is unavailable: {_LIGHTSTIM_IMPORT_ERROR}",
)
class AtomicTimelineAdapterTest(unittest.TestCase):
    def test_ideal_timeline_builds_and_samples_cleanly(self) -> None:
        circuit = AtomicTimelineExperiment(descriptor=_descriptor()).build()

        self.assertGreater(circuit.num_qubits, 0)
        self.assertGreater(circuit.num_detectors, 0)
        self.assertGreater(circuit.num_observables, 0)

        detectors, observables = circuit.compile_detector_sampler().sample(
            shots=20, separate_observables=True
        )
        self.assertFalse(detectors.any())
        self.assertFalse(observables.any())

    def test_unsupported_gates_raise(self) -> None:
        cases = (
            ("logical_x", ["left"]),
            ("logical_y", ["left"]),
            ("logical_z", ["left"]),
            ("logical_cz", ["left", "right"]),
            ("logical_t", ["left"]),
            ("logical_toffoli", ["left", "right", "ancilla"]),
        )
        for gate_name, patch_ids in cases:
            with self.subTest(gate=gate_name):
                descriptor = _descriptor()
                if gate_name == "logical_toffoli":
                    descriptor["patches"].append(
                        {
                            "id": "ancilla",
                            "code": "unrotated_surface_code",
                            "distance": 3,
                            "offset": [20, 0],
                            "initial_basis": "Z",
                            "measurement_basis": "Z",
                        }
                    )
                    descriptor["timeline"].insert(
                        2,
                        {"op": "initialize", "patch": "ancilla", "basis": "Z"},
                    )
                unitary = next(
                    entry
                    for entry in descriptor["timeline"]
                    if entry["op"] == "unitary_block"
                )
                unitary["gates"] = [{"gate": gate_name, "patches": patch_ids}]

                with self.assertRaises(AtomicTimelineError):
                    AtomicTimelineExperiment(descriptor=descriptor).build()

    def test_s_and_cnot_add_physical_gate_instructions(self) -> None:
        descriptor = _descriptor()
        descriptor["patches"][0]["initial_basis"] = "Z"
        descriptor["patches"][0]["measurement_basis"] = "Z"
        descriptor["patches"][1]["distance"] = 3
        descriptor["patches"][1]["offset"] = [10, 0]
        prefix = [
            {"op": "initialize", "patch": "left", "basis": "Z"},
            {"op": "initialize", "patch": "right", "basis": "Z"},
            {"op": "syndrome_extraction", "rounds": 1},
        ]
        suffix = [
            {"op": "syndrome_extraction", "rounds": 2},
            {
                "op": "data_readout",
                "measurements": [
                    {"patch": "left", "basis": "Z"},
                    {"patch": "right", "basis": "Z"},
                ],
            },
        ]

        descriptor["timeline"] = prefix + suffix
        baseline = AtomicTimelineExperiment(descriptor=descriptor).build().flattened()

        cases = (
            ("logical_s", [{"gate": "logical_s", "patches": ["left"]}], "S"),
            (
                "logical_cnot",
                [{"gate": "logical_cnot", "patches": ["left", "right"]}],
                "CX",
            ),
        )
        for label, gates, physical_instruction in cases:
            with self.subTest(gate=label):
                gated_descriptor = copy.deepcopy(descriptor)
                gated_descriptor["timeline"] = (
                    prefix
                    + [{"op": "unitary_block", "gates": gates}]
                    + suffix
                )
                circuit = AtomicTimelineExperiment(
                    descriptor=gated_descriptor
                ).build()
                flattened = circuit.flattened()

                baseline_count = sum(
                    instruction.name == physical_instruction
                    for instruction in baseline
                )
                gated_count = sum(
                    instruction.name == physical_instruction
                    for instruction in flattened
                )
                self.assertGreater(gated_count, baseline_count)
                self.assertNotEqual(str(flattened), str(baseline))

                detectors, observables = circuit.compile_detector_sampler().sample(
                    shots=20, separate_observables=True
                )
                self.assertFalse(detectors.any())
                self.assertFalse(observables.any())

    def test_legacy_direct_readout_synthesizes_baseline(self) -> None:
        for label, gates in (
            ("initialize-to-readout", None),
            ("hadamard-to-readout", [{"gate": "logical_h", "patches": ["left"]}]),
        ):
            with self.subTest(shape=label):
                descriptor = _descriptor()
                left_initial_basis = "Z" if gates is not None else "X"
                descriptor["patches"][0]["initial_basis"] = left_initial_basis
                timeline = [
                    {
                        "op": "initialize",
                        "patch": "left",
                        "basis": left_initial_basis,
                    },
                    {"op": "initialize", "patch": "right", "basis": "Z"},
                ]
                if gates is not None:
                    timeline.append({"op": "unitary_block", "gates": gates})
                timeline.append(
                    {
                        "op": "data_readout",
                        "measurements": [
                            {"patch": "left", "basis": "X"},
                            {"patch": "right", "basis": "Z"},
                        ],
                    }
                )
                descriptor["timeline"] = timeline

                circuit = AtomicTimelineExperiment(descriptor=descriptor).build()
                self.assertGreater(circuit.num_detectors, 0)
                self.assertGreater(circuit.num_observables, 0)
                detectors, observables = circuit.compile_detector_sampler().sample(
                    shots=20, separate_observables=True
                )
                self.assertFalse(detectors.any())
                self.assertFalse(observables.any())

    def test_immediate_surgery_matches_emitter_shape(self) -> None:
        descriptor = _descriptor()
        descriptor["patches"][0]["measurement_basis"] = "Z"
        descriptor["patches"][1]["distance"] = 3
        descriptor["patches"][1]["measurement_basis"] = "X"
        descriptor["timeline"] = [
            {"op": "initialize", "patch": "left", "basis": "X"},
            {"op": "initialize", "patch": "right", "basis": "Z"},
            {
                "op": "activate_coupler",
                "name": "bridge",
                "interaction": "ZZ",
                "patches": ["left", "right"],
            },
            {"op": "syndrome_extraction", "rounds": 2},
            {"op": "deactivate_coupler", "name": "bridge"},
            {
                "op": "data_readout",
                "measurements": [
                    {"patch": "left", "basis": "Z"},
                    {"patch": "right", "basis": "X"},
                ],
            },
        ]

        circuit = AtomicTimelineExperiment(descriptor=descriptor).build()
        self.assertGreater(circuit.num_observables, 0)

    def test_bad_lifecycle_and_unknown_patch_raise(self) -> None:
        lifecycle = _descriptor()
        lifecycle["timeline"][4] = {
            "op": "deactivate_coupler",
            "name": "bridge",
        }

        unknown_patch = copy.deepcopy(_descriptor())
        unknown_patch["timeline"][2]["gates"][0]["patches"] = ["missing"]

        unequal_cnot = copy.deepcopy(_descriptor())
        unequal_cnot["timeline"][2]["gates"] = [
            {"gate": "logical_cnot", "patches": ["left", "right"]}
        ]

        for label, descriptor in (
            ("deactivate-before-activate", lifecycle),
            ("unknown-patch", unknown_patch),
            ("unequal-distance-cnot", unequal_cnot),
        ):
            with self.subTest(label=label):
                with self.assertRaises(AtomicTimelineError):
                    AtomicTimelineExperiment(descriptor=descriptor).build()


if __name__ == "__main__":
    unittest.main()
