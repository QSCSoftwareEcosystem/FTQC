# LightStim atomic adapter

`lightstim_atomic_adapter.py` provides a compatibility layer for the version-2
`ftqc-lightstim-atomic-*` JSON formats developed on the FTQC atomic branch. It
maps the emitted timeline onto LightStim's public patch, builder, coupler,
logical-operation, noise, and simulation APIs. Keeping the implementation next
to the FTQC emitter provides a concrete integration point while collaborators
validate the intended interface and decide how it should evolve longer term.

## Start from the compatibility branch

The compatibility branch is a direct successor of `lightstim.atomic`; no files
need to be copied manually. To evaluate it in an existing FTQC checkout:

```bash
git fetch origin
git switch --track origin/fix/lightstim-atomic-adapter
```

Point the runner at the LightStim checkout and Python environment to test:

```bash
export LIGHTSTIM_REPO=/absolute/path/to/LightStim
export LIGHTSTIM_PYTHON=/absolute/path/to/LightStim/venv/bin/python
```

After validating the adapter tests and the MLIR-to-LER example below, the fix
can be proposed for merge back into `lightstim.atomic`. Subsequent atomic work
can then continue from the updated `lightstim.atomic` branch or from short-lived
feature branches based on it. If an existing internal branch has diverged,
merge or cherry-pick compatibility commit `6bfd916` into a temporary integration
branch and run the same checks there.

## Supported subset

- unrotated surface-code patches with X/Z initialization and readout;
- syndrome-extraction windows;
- one active XX or ZZ two-patch coupler at a time;
- logical H, logical S, and equal-size transversal CNOT;
- terminal full-patch readout and optional circuit-level noise.

The adapter rejects unknown lifecycle transitions and operations. Logical T
and Toffoli require a verified magic-state lowering. Logical Pauli and CZ gates
are also rejected until their physical lowering is validated. This is
intentional: integrated execution fails instead of silently dropping semantics.

Coupler corridor initialization/readout is inferred from the interaction
(`ZZ -> X`, `XX -> Z`). LightStim versions with absorbed-logical tracking use
`resolve_absorbed=False` for the corridor readout; older versions are detected
from the public method signature and use the legacy call.

The original atomic emitter could activate its first coupler, or perform terminal
readout, immediately after patch initialization or a logical unitary. For those
existing descriptors the adapter inserts one unmerged syndrome-extraction round
before activation/readout. The repaired emitter now serializes that baseline
explicitly, using `default-se-rounds`.

## Checkout discovery

`run_lightstim_descriptor.py` tries, in order:

1. `LIGHTSTIM_REPO`;
2. a sibling `LightStim` checkout;
3. a sibling `lightstim` checkout;
4. an already installed `lightstim` package.

`run_ftqc_to_ler.py` uses this repository's `build/src/ftqc-opt`. For the
simulation subprocess it prefers `LIGHTSTIM_PYTHON`, then a sibling
`LightStim/{venv,.venv}/bin/python`, and finally the current interpreter.

## Test and run

First run the adapter regression tests against the selected LightStim checkout:

```bash
PYTHONPATH=/path/to/LightStim:$PWD \
  /path/to/LightStim/venv/bin/python -m unittest discover -v \
  -s test -p 'test_lightstim_atomic_adapter.py'
```

Then run either an already-emitted descriptor or the complete FTQC MLIR example:

```bash
python run_lightstim_descriptor.py path/to/atomic_descriptor.json

python run_ftqc_to_ler.py test/Integration/atomic-test-input.mlir \
  --ftqc-opt build/src/ftqc-opt
```

The second command exercises FTQC MLIR emission, descriptor construction,
LightStim circuit building, the configured decoder, and LER report generation.
It requires an existing FTQC/MLIR build containing `build/src/ftqc-opt`.

The compatibility test has been exercised against the LightStim snapshot from
immediately before the FTQC atomic runner (`b6b645a`) and current main at the
time of the repair (`59e5ba5`).
