#!/usr/bin/env python3
"""Monte Carlo MWPM simulation of compiler-generated Steane [[7,1,3]] STIM.

Usage: realqpu/bin/python utils/sim_stim_ftqc_steane.py --input circuit.stim

Sweep the same 18 physical error rates (1e-4 to about 1.5e-2) as
sim_stim_ftqc_loop.py. Existing DEPOLARIZE1/2 and X/Y/Z_ERROR probabilities
are replaced by p, preserving their locations (including inside REPEAT blocks).
If none are present, add DEPOLARIZE1/2 after Clifford gates and basis-flip
errors after resets and before measurements, as in the compiler noise model.
Other stochastic channels and noisy measurement arguments are rejected to
avoid mixing fixed noise with swept noise. The input must already contain
Steane encoding, syndrome extraction, DETECTORs and OBSERVABLE_INCLUDEs;
this script does not encode a bare circuit or invoke ftqc-opt.

p_shot is the fraction of shots with ANY incorrectly decoded observable.
Without observables it is unavailable (an empty CSV field). Non-deterministic
detectors/observables and models that cannot be decoded by MWPM are errors.
"""

import argparse
import csv
from pathlib import Path
import sys

import numpy as np
import pymatching
import stim


ERROR_RATES = np.logspace(-4, -1.82, 18)
NOISE_GATES = {"DEPOLARIZE1", "DEPOLARIZE2", "X_ERROR", "Y_ERROR", "Z_ERROR"}
CSV_FIELDS = [
    "benchmark", "n_logical", "n_phys", "p", "p_shot", "detectors", "observables"
]


def has_noise(circuit: stim.Circuit) -> bool:
    """Validate the swept noise model and detect explicit noise locations."""
    found = False
    for instruction in circuit:
        if isinstance(instruction, stim.CircuitRepeatBlock):
            found = has_noise(instruction.body_copy()) or found
            continue
        name = instruction.name
        data = stim.gate_data(name)
        if name in NOISE_GATES:
            found = True
        elif data.is_noisy_gate and not data.produces_measurements:
            raise ValueError(f"Unsupported noise instruction: {name}")
        elif data.produces_measurements and any(instruction.gate_args_copy()):
            raise ValueError(f"Nonzero measurement noise is unsupported: {instruction}")
    return found


def compile_stim_circuit(
    source: stim.Circuit, p: float, *, explicit_noise: bool
) -> stim.Circuit:
    """Create one sweep circuit without changing its QEC structure."""
    circuit = stim.Circuit()
    for instruction in source:
        if isinstance(instruction, stim.CircuitRepeatBlock):
            body = compile_stim_circuit(
                instruction.body_copy(), p, explicit_noise=explicit_noise
            )
            circuit.append(stim.CircuitRepeatBlock(instruction.repeat_count, body))
            continue
        name = instruction.name
        targets = instruction.targets_copy()
        if name in NOISE_GATES:
            circuit.append(name, targets, p)
            continue
        if explicit_noise:
            circuit.append(instruction)
            continue

        data = stim.gate_data(name)
        # Compiler output uses ordinary single-qubit M/MR and R instructions.
        basis_flip = "Z_ERROR" if name in {"MX", "MRX", "RX"} else "X_ERROR"
        if data.produces_measurements:
            if name not in {"M", "MX", "MY", "MR", "MRX", "MRY"}:
                raise ValueError(f"Cannot infer measurement noise for {name}")
            circuit.append(basis_flip, [t.value for t in targets], p)
        circuit.append(instruction)
        if data.is_reset:
            circuit.append(basis_flip, [t.value for t in targets], p)
        elif data.is_unitary:
            if data.is_single_qubit_gate:
                circuit.append("DEPOLARIZE1", targets, p)
            elif data.is_two_qubit_gate:
                # Record-controlled corrections have no physical two-qubit gate.
                pairs = []
                for k in range(0, len(targets), 2):
                    pair = targets[k:k + 2]
                    if all(t.is_qubit_target for t in pair):
                        pairs.extend(pair)
                if pairs:
                    circuit.append("DEPOLARIZE2", pairs, p)
            else:
                raise ValueError(f"Cannot infer gate noise for {name}")
    return circuit


def simulate(circuit: stim.Circuit, num_shots: int) -> float | None:
    """Decode detector samples and count shots with any logical failure."""
    if circuit.num_observables == 0:
        return None
    dem = circuit.detector_error_model(
        decompose_errors=True, approximate_disjoint_errors=True
    )
    matcher = pymatching.Matching.from_detector_error_model(dem)
    sampler = circuit.compile_detector_sampler()
    failures = 0
    # Bound sample memory for large shot counts.
    for start in range(0, num_shots, 10_000):
        count = min(10_000, num_shots - start)
        detectors, observables = sampler.sample(count, separate_observables=True)
        predictions = matcher.decode_batch(detectors)
        failures += int(np.count_nonzero(np.any(predictions != observables, axis=1)))
    return failures / num_shots


def run_sweep(input_file: Path, num_shots: int, output_csv: Path) -> None:
    source = stim.Circuit.from_file(input_file)
    if not len(source):
        raise ValueError("Input STIM circuit is empty")
    explicit_noise = has_noise(source)
    print(f"Input: {input_file} | shots per point: {num_shots:,}")
    print(f"Circuit: {source.num_qubits} qubits, {source.num_detectors} detectors, "
          f"{source.num_observables} observables")
    print("Noise: " + ("replace existing probabilities" if explicit_noise
                       else "insert compiler-style gate/reset/measurement noise"))
    if not source.num_observables:
        print("WARNING: no logical observables; p_shot will be unavailable.")
    results = []
    for p in ERROR_RATES:
        circuit = compile_stim_circuit(source, float(p), explicit_noise=explicit_noise)
        try:
            p_shot = simulate(circuit, num_shots)
        except (ValueError, RuntimeError) as exc:
            raise ValueError(f"Simulation failed at p={p:.6g}: {exc}") from exc
        print(f"p={p:.6e}  p_shot=" +
              (f"{p_shot:.6e}" if p_shot is not None else "unavailable"))
        results.append(dict(
            benchmark=input_file.stem,
            # STIM does not encode the number of Steane logical blocks.
            n_logical=None,
            n_phys=circuit.num_qubits, p=float(p), p_shot=p_shot,
            detectors=circuit.num_detectors, observables=circuit.num_observables,
        ))
    with output_csv.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(results)
    print(f"Results saved to: {output_csv}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--shots", type=int, default=5000,
                        help="Shots per (input circuit, p) point (default: 5000)")
    parser.add_argument("--input", type=Path, required=True, help="Input STIM file path")
    parser.add_argument("--output", type=Path, default=Path("stim_ftqc_steane_results.csv"),
                        help="CSV output path (default: stim_ftqc_steane_results.csv)")
    args = parser.parse_args()
    if args.shots <= 0:
        parser.error("--shots must be positive")
    if args.input.resolve() == args.output.resolve():
        parser.error("--output must differ from --input")
    try:
        run_sweep(args.input, args.shots, args.output)
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
