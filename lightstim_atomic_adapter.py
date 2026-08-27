"""Execute FTQC atomic-timeline descriptors with LightStim's public API."""

from __future__ import annotations

import copy
import inspect
import math
from collections.abc import Mapping, Sequence
from typing import Any, Optional

import stim

from lightstim.ir.builder import CircuitBuilder
from lightstim.ir.qec_system import QECSystem
from lightstim.ir.tracker import SyndromeTracker
from lightstim.noise.config import NoiseConfig
from lightstim.qec_code.surface_code.unrotated import (
    UnrotatedSurfaceCode,
    UnrotatedSurfaceCodeExtractionBlock,
    UnrotatedSurfaceCodeLogicalOpSet,
    UnrotatedTwoPatchCoupler,
)


class AtomicTimelineError(ValueError):
    """An invalid or unsupported FTQC atomic timeline."""


class AtomicTimelineExperiment:
    """Build an unrotated-surface-code circuit from an FTQC v2 timeline."""

    _FORMATS = {
        "ftqc-lightstim-atomic-experiment",
        "ftqc-lightstim-atomic-ir",
    }
    _BASES = {"X", "Z"}
    _ARITY = {"logical_h": 1, "logical_s": 1, "logical_cnot": 2}
    _UNSUPPORTED_GATES = {
        "logical_x",
        "logical_y",
        "logical_z",
        "logical_cz",
    }
    _NON_CLIFFORD_GATES = {"logical_t", "logical_toffoli"}

    def __init__(
        self,
        descriptor: Mapping[str, Any],
        noise_params: Optional[NoiseConfig] = None,
        noise_model: str = "circuit_level",
        if_detector: bool = True,
    ) -> None:
        if not isinstance(descriptor, Mapping):
            raise AtomicTimelineError("descriptor must be a mapping")
        self.descriptor = copy.deepcopy(dict(descriptor))
        self.noise_params = noise_params
        self.noise_model = noise_model
        self.if_detector = if_detector

        self.system: Optional[QECSystem] = None
        self.tracker: Optional[SyndromeTracker] = None
        self.builder: Optional[CircuitBuilder] = None
        self._patch_specs: dict[str, Mapping[str, Any]] = {}
        self._global_patches: dict[str, UnrotatedSurfaceCode] = {}
        self._coupler_specs: dict[str, tuple[str, tuple[str, str]]] = {}
        self._corridor_data: dict[str, dict[int, str]] = {}
        self._active_coupler: Optional[str] = None
        self._needs_baseline_se = False

        self._validate_descriptor()
        self._validate_timeline()

    @staticmethod
    def _object(value: Any, where: str) -> Mapping[str, Any]:
        if not isinstance(value, Mapping):
            raise AtomicTimelineError(f"{where} must be an object")
        return value

    @staticmethod
    def _list(value: Any, where: str) -> Sequence[Any]:
        if not isinstance(value, Sequence) or isinstance(value, (str, bytes)):
            raise AtomicTimelineError(f"{where} must be a list")
        return value

    @staticmethod
    def _integer(value: Any, where: str, *, minimum: int = 1) -> int:
        if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
            raise AtomicTimelineError(f"{where} must be an integer >= {minimum}")
        return value

    @classmethod
    def _basis(cls, value: Any, where: str) -> str:
        basis = str(value).upper()
        if basis not in cls._BASES:
            raise AtomicTimelineError(f"{where} must be X or Z")
        return basis

    def _patch_ids(self, value: Any, where: str, arity: int) -> tuple[str, ...]:
        items = self._list(value, where)
        if len(items) != arity:
            raise AtomicTimelineError(f"{where} must contain exactly {arity} patch id(s)")
        ids = tuple(str(item) for item in items)
        if len(set(ids)) != len(ids):
            raise AtomicTimelineError(f"{where} must not contain duplicate patch ids")
        unknown = [patch_id for patch_id in ids if patch_id not in self._patch_specs]
        if unknown:
            raise AtomicTimelineError(f"{where} references unknown patch(es): {unknown}")
        return ids

    def _validate_descriptor(self) -> None:
        descriptor = self.descriptor
        if descriptor.get("format") not in self._FORMATS or descriptor.get("version") != 2:
            raise AtomicTimelineError("descriptor must use an FTQC atomic format at version 2")
        experiment = self._object(descriptor.get("experiment"), "experiment")
        if experiment.get("type") != "atomic_timeline":
            raise AtomicTimelineError("experiment.type must be 'atomic_timeline'")

        patches = self._list(descriptor.get("patches"), "patches")
        if not patches:
            raise AtomicTimelineError("patches must not be empty")
        for index, raw_patch in enumerate(patches):
            where = f"patches[{index}]"
            patch = self._object(raw_patch, where)
            patch_id = patch.get("id")
            if not isinstance(patch_id, str) or not patch_id:
                raise AtomicTimelineError(f"{where}.id must be a non-empty string")
            if patch_id in self._patch_specs:
                raise AtomicTimelineError(f"duplicate patch id {patch_id!r}")
            if patch.get("code") != "unrotated_surface_code":
                raise AtomicTimelineError(
                    f"{where}.code must be 'unrotated_surface_code'"
                )
            self._integer(patch.get("distance"), f"{where}.distance", minimum=2)
            offset = self._list(patch.get("offset"), f"{where}.offset")
            if len(offset) != 2 or any(
                isinstance(coord, bool)
                or not isinstance(coord, (int, float))
                or not math.isfinite(float(coord))
                for coord in offset
            ):
                raise AtomicTimelineError(f"{where}.offset must contain two finite numbers")
            self._basis(patch.get("initial_basis"), f"{where}.initial_basis")
            self._basis(
                patch.get("measurement_basis"), f"{where}.measurement_basis"
            )
            self._patch_specs[patch_id] = patch

        timeline = self._list(descriptor.get("timeline"), "timeline")
        if not timeline:
            raise AtomicTimelineError("timeline must not be empty")

    def _validate_timeline(self) -> None:
        states = {patch_id: "new" for patch_id in self._patch_specs}
        active_coupler: Optional[str] = None
        active_coupler_had_se = False
        seen_couplers: set[str] = set()
        timeline = self._list(self.descriptor["timeline"], "timeline")

        for index, raw_entry in enumerate(timeline):
            where = f"timeline[{index}]"
            entry = self._object(raw_entry, where)
            op = entry.get("op")
            if not isinstance(op, str):
                raise AtomicTimelineError(f"{where}.op must be a string")

            if op == "initialize":
                patch_id = str(entry.get("patch"))
                if patch_id not in states:
                    raise AtomicTimelineError(f"{where}.patch references unknown patch {patch_id!r}")
                if states[patch_id] != "new":
                    raise AtomicTimelineError(f"{where}: patch {patch_id!r} is already initialized")
                basis = self._basis(entry.get("basis"), f"{where}.basis")
                expected = self._basis(
                    self._patch_specs[patch_id]["initial_basis"],
                    f"patch {patch_id!r} initial_basis",
                )
                if basis != expected:
                    raise AtomicTimelineError(
                        f"{where}.basis {basis!r} disagrees with patch metadata {expected!r}"
                    )
                states[patch_id] = "live"

            elif op == "syndrome_extraction":
                self._integer(entry.get("rounds", 1), f"{where}.rounds")
                not_live = [name for name, state in states.items() if state != "live"]
                if not_live:
                    raise AtomicTimelineError(
                        f"{where}: syndrome extraction requires every patch live; got {not_live}"
                    )
                if active_coupler is not None:
                    active_coupler_had_se = True

            elif op == "activate_coupler":
                name = entry.get("name")
                if not isinstance(name, str) or not name:
                    raise AtomicTimelineError(f"{where}.name must be a non-empty string")
                if active_coupler is not None:
                    raise AtomicTimelineError(
                        f"{where}: coupler {active_coupler!r} is already active"
                    )
                if name in seen_couplers:
                    raise AtomicTimelineError(f"{where}: coupler name {name!r} is reused")
                interaction = str(entry.get("interaction")).upper()
                if interaction not in {"XX", "ZZ"}:
                    raise AtomicTimelineError(f"{where}.interaction must be XX or ZZ")
                patch_ids = self._patch_ids(entry.get("patches"), f"{where}.patches", 2)
                if any(states[patch_id] != "live" for patch_id in patch_ids):
                    raise AtomicTimelineError(f"{where}: coupler patches must be live")
                seen_couplers.add(name)
                active_coupler = name
                active_coupler_had_se = False
                self._coupler_specs[name] = (
                    interaction,
                    (patch_ids[0], patch_ids[1]),
                )

            elif op == "deactivate_coupler":
                name = entry.get("name")
                if name != active_coupler:
                    raise AtomicTimelineError(
                        f"{where}: cannot deactivate {name!r}; active coupler is {active_coupler!r}"
                    )
                if not active_coupler_had_se:
                    raise AtomicTimelineError(
                        f"{where}: coupler {name!r} has no syndrome-extraction round"
                    )
                active_coupler = None
                active_coupler_had_se = False

            elif op == "unitary_block":
                if active_coupler is not None:
                    raise AtomicTimelineError(
                        f"{where}: unitary gates are unsupported while a coupler is active"
                    )
                gates = self._list(entry.get("gates"), f"{where}.gates")
                if not gates:
                    raise AtomicTimelineError(f"{where}.gates must not be empty")
                for gate_index, raw_gate in enumerate(gates):
                    gate_where = f"{where}.gates[{gate_index}]"
                    gate = self._object(raw_gate, gate_where)
                    gate_name = str(gate.get("gate")).lower()
                    if gate_name in self._NON_CLIFFORD_GATES:
                        raise AtomicTimelineError(
                            f"{gate_where}: {gate_name} requires a verified magic-state protocol"
                        )
                    if gate_name in self._UNSUPPORTED_GATES:
                        raise AtomicTimelineError(
                            f"{gate_where}: {gate_name} is not implemented by this adapter"
                        )
                    if gate_name not in self._ARITY:
                        raise AtomicTimelineError(f"{gate_where}: unsupported gate {gate_name!r}")
                    patch_ids = self._patch_ids(
                        gate.get("patches"),
                        f"{gate_where}.patches",
                        self._ARITY[gate_name],
                    )
                    if gate_name == "logical_cnot" and (
                        self._patch_specs[patch_ids[0]]["distance"]
                        != self._patch_specs[patch_ids[1]]["distance"]
                    ):
                        raise AtomicTimelineError(
                            f"{gate_where}: logical_cnot requires equal-distance patches"
                        )
                    if any(states[patch_id] != "live" for patch_id in patch_ids):
                        raise AtomicTimelineError(f"{gate_where}: gate patches must be live")
            elif op == "data_readout":
                if active_coupler is not None:
                    raise AtomicTimelineError(
                        f"{where}: deactivate the active coupler before patch readout"
                    )
                if index != len(timeline) - 1:
                    raise AtomicTimelineError(f"{where}: patch data_readout must be terminal")
                measurements = self._list(
                    entry.get("measurements"), f"{where}.measurements"
                )
                measured: set[str] = set()
                for measurement_index, raw_measurement in enumerate(measurements):
                    measurement_where = f"{where}.measurements[{measurement_index}]"
                    measurement = self._object(raw_measurement, measurement_where)
                    patch_id = str(measurement.get("patch"))
                    if patch_id not in states:
                        raise AtomicTimelineError(
                            f"{measurement_where}.patch references unknown patch {patch_id!r}"
                        )
                    if patch_id in measured:
                        raise AtomicTimelineError(
                            f"{measurement_where}: duplicate readout for patch {patch_id!r}"
                        )
                    if states[patch_id] != "live":
                        raise AtomicTimelineError(
                            f"{measurement_where}: patch {patch_id!r} is not live"
                        )
                    basis = self._basis(
                        measurement.get("basis"), f"{measurement_where}.basis"
                    )
                    expected = self._basis(
                        self._patch_specs[patch_id]["measurement_basis"],
                        f"patch {patch_id!r} measurement_basis",
                    )
                    if basis != expected:
                        raise AtomicTimelineError(
                            f"{measurement_where}.basis {basis!r} disagrees with "
                            f"patch metadata {expected!r}"
                        )
                    measured.add(patch_id)
                    states[patch_id] = "read"
                if measured != set(states):
                    missing = sorted(set(states) - measured)
                    raise AtomicTimelineError(f"{where}: terminal readout misses patches {missing}")

            else:
                raise AtomicTimelineError(f"{where}: unsupported op {op!r}")

        if active_coupler is not None:
            raise AtomicTimelineError(f"timeline ends with active coupler {active_coupler!r}")
        unfinished = [name for name, state in states.items() if state != "read"]
        if unfinished:
            raise AtomicTimelineError(f"timeline does not read out patches {unfinished}")

    def _setup(self) -> None:
        self.system = QECSystem()
        self._global_patches = {}
        for patch_id, patch in self._patch_specs.items():
            code = UnrotatedSurfaceCode(distance=int(patch["distance"]))
            self._global_patches[patch_id] = self.system.add_patch(
                code,
                name=patch_id,
                offset=tuple(patch["offset"]),
            )

        for name, (interaction, patch_ids) in self._coupler_specs.items():
            self.system.register_coupler(
                UnrotatedTwoPatchCoupler(),
                patch_names=list(patch_ids),
                name=name,
                interaction_type=interaction,
            )

        self.tracker = SyndromeTracker(
            num_qubits=self.system.num_qubits,
            expected_num_logicals=self.system.num_logicals,
        )
        self.builder = CircuitBuilder(
            tracker=self.tracker,
            system_config=self.system,
            if_detector=self.if_detector,
        )
        self.builder.write_coordinates()
        self._active_coupler = None
        self._needs_baseline_se = False
        self._corridor_data = {}

    def _code_data(self, patch_id: str) -> set[int]:
        return set(self._global_patches[patch_id].data_indices)

    def _apply_syndrome_extraction(self, rounds: int) -> None:
        assert self.system is not None and self.builder is not None
        block = UnrotatedSurfaceCodeExtractionBlock(self.system)
        self.builder.apply_syndrome_extraction(
            circuit_chunk=block.circuit,
            rounds=rounds,
        )

    def _activate(self, entry: Mapping[str, Any]) -> None:
        assert self.system is not None and self.builder is not None
        name = str(entry["name"])
        interaction, _ = self._coupler_specs[name]
        # Descriptors emitted by the original atomic branch activated a coupler
        # immediately after initialization/a unitary block. LightStim needs an
        # unmerged extraction round to establish the patch stabilizer baseline.
        if self._needs_baseline_se:
            self._apply_syndrome_extraction(rounds=1)
            self._needs_baseline_se = False
        self.builder.activate_coupler(name)
        self._active_coupler = name
        local_to_global = self.system.local_to_global_map[name]
        corridor = self.system.coupler_patches[name]
        basis = "X" if interaction == "ZZ" else "Z"
        data = {local_to_global[index]: basis for index in corridor.data_indices}
        if not data:
            raise AtomicTimelineError(f"coupler {name!r} has no corridor data qubits")
        self.builder.initialize(data, n=self.system.num_qubits)
        self._corridor_data[name] = data

    def _deactivate(self, name: str) -> None:
        assert self.builder is not None
        data = self._corridor_data.pop(name)
        self.builder.deactivate_coupler(name)
        readout_parameters = inspect.signature(
            self.builder.apply_data_readout
        ).parameters
        kwargs: dict[str, Any] = {"final_measurements": data}
        if "resolve_absorbed" in readout_parameters:
            kwargs["resolve_absorbed"] = False
        self.builder.apply_data_readout(**kwargs)
        self._active_coupler = None

    def _unitary(self, entry: Mapping[str, Any]) -> None:
        assert self.system is not None and self.builder is not None
        op_set = UnrotatedSurfaceCodeLogicalOpSet(
            UnrotatedSurfaceCodeExtractionBlock
        )
        for raw_gate in self._list(entry["gates"], "unitary_block.gates"):
            gate = self._object(raw_gate, "unitary gate")
            gate_name = str(gate["gate"]).lower()
            patch_ids = tuple(str(item) for item in gate["patches"])
            if gate_name == "logical_h":
                op_set.fold_transversal_hadamard(
                    self.builder,
                    self.system.patches[patch_ids[0]][0],
                )
            elif gate_name == "logical_s":
                op_set.fold_transversal_s(
                    self.builder,
                    self.system.patches[patch_ids[0]][0],
                )
            else:
                op_set.transversal_cnot(
                    self.builder,
                    self._global_patches[patch_ids[0]],
                    self._global_patches[patch_ids[1]],
                )
        self._needs_baseline_se = True

    def _execute(self, entry: Mapping[str, Any]) -> None:
        assert self.system is not None and self.builder is not None
        op = str(entry["op"])
        if op == "initialize":
            patch_id = str(entry["patch"])
            basis = self._basis(entry["basis"], "initialize.basis")
            self.builder.initialize(
                {index: basis for index in self._code_data(patch_id)},
                n=self.system.num_qubits,
            )
            self._needs_baseline_se = True
        elif op == "syndrome_extraction":
            self._apply_syndrome_extraction(rounds=int(entry.get("rounds", 1)))
            if self._active_coupler is None:
                self._needs_baseline_se = False
        elif op == "activate_coupler":
            self._activate(entry)
        elif op == "deactivate_coupler":
            self._deactivate(str(entry["name"]))
        elif op == "unitary_block":
            self._unitary(entry)
        else:
            # Legacy descriptors can also go straight from initialization or a
            # unitary block to terminal readout. Establish the same baseline
            # that the repaired emitter now writes explicitly.
            if self._needs_baseline_se:
                self._apply_syndrome_extraction(rounds=1)
                self._needs_baseline_se = False
            measurements: dict[int, str] = {}
            for raw_measurement in entry["measurements"]:
                measurement = self._object(raw_measurement, "data_readout measurement")
                basis = self._basis(measurement["basis"], "data_readout basis")
                measurements.update(
                    {
                        index: basis
                        for index in self._code_data(str(measurement["patch"]))
                    }
                )
            self.builder.apply_data_readout(final_measurements=measurements)

    def build(self) -> stim.Circuit:
        """Construct the clean circuit, then apply the requested noise model."""
        try:
            self._setup()
        except (AtomicTimelineError, KeyboardInterrupt):
            raise
        except Exception as exc:
            raise AtomicTimelineError(f"failed to construct patch layout: {exc}") from exc

        timeline = self._list(self.descriptor["timeline"], "timeline")
        for index, raw_entry in enumerate(timeline):
            entry = self._object(raw_entry, f"timeline[{index}]")
            try:
                self._execute(entry)
            except (AtomicTimelineError, KeyboardInterrupt):
                raise
            except Exception as exc:
                raise AtomicTimelineError(
                    f"timeline[{index}] {entry.get('op')!r} failed: {exc}"
                ) from exc

        assert self.builder is not None
        if self.if_detector and self.builder.circuit.num_observables == 0:
            raise AtomicTimelineError(
                "atomic timeline produced no logical observables; refusing a meaningless LER"
            )
        if self.noise_params is not None:
            try:
                return self.builder.build_noisy_circuit(
                    noise_params=self.noise_params,
                    noise_model=self.noise_model,
                )
            except Exception as exc:
                raise AtomicTimelineError(f"noise injection failed: {exc}") from exc
        return self.builder.circuit


__all__ = ["AtomicTimelineError", "AtomicTimelineExperiment"]
