# LightStim atomic adapter

`lightstim_atomic_adapter.py` is the FTQC-owned compatibility boundary for the
version-2 `ftqc-lightstim-atomic-*` JSON formats. It maps the compiler-owned
timeline onto LightStim's public patch, builder, coupler, logical-operation,
noise, and simulation APIs; the FTQC schema is not installed into LightStim.

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

## Run

```bash
python run_lightstim_descriptor.py path/to/atomic_descriptor.json

python run_ftqc_to_ler.py test/Integration/atomic-test-input.mlir \
  --ftqc-opt build/src/ftqc-opt
```

The adapter regression test can be run against a selected LightStim checkout:

```bash
PYTHONPATH=/path/to/LightStim:$PWD \
  /path/to/LightStim/venv/bin/python -m unittest discover -v \
  -s test -p 'test_lightstim_atomic_adapter.py'
```

The compatibility test has been exercised against the LightStim snapshot from
immediately before the FTQC atomic runner (`b6b645a`) and current main at the
time of the repair (`59e5ba5`).
