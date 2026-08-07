---
marp: true
theme: default
paginate: true
math: mathjax
style: |
  section {
    font-size: 1.6rem;
  }
  section.lead h1 {
    font-size: 2.6rem;
  }
  code {
    font-size: 0.85em;
  }
  pre code {
    font-size: 0.78em;
  }
  h2 { border-bottom: 2px solid #4a90d9; padding-bottom: 4px; }
  .columns { display: grid; grid-template-columns: 1fr 1fr; gap: 1.5rem; }
---

<!-- _class: lead -->

# FTQC MLIR Dialect

### A Fault-Tolerant Quantum Computing Compiler Infrastructure

Built on LLVM/MLIR 22 · Clifford+T · Steane · Surface · Color Code

---

## Motivation

**The fault-tolerance gap**: today's NISQ devices have physical error rates $p \sim 10^{-2}$–$10^{-3}$.
Useful algorithms (Shor, quantum chemistry) require logical error rates $p_L < 10^{-10}$.

**Quantum error correction (QEC)** encodes one logical qubit in many physical qubits and detects/corrects errors before they accumulate.

**The compiler's job**:

| Task | Traditional | FTQC |
|------|------------|------|
| Logical gates | H, CNOT, T | transversal gates + magic injection |
| Error model | none | syndrome extraction, EC rounds |
| Resource budget | gate count | physical qubits, T-gate count, distillation |

> **FTQC** makes this pipeline a first-class citizen of the MLIR compiler stack.

---

## What Is an MLIR Dialect?

MLIR is a reusable compiler infrastructure for representing and transforming programs at multiple levels of abstraction.

```
┌─────────────────────────────────────────────┐
│  MLIR Module                                │
│  ┌─────────────────┐  ┌──────────────────┐  │
│  │  func Dialect   │  │  FTQC Dialect    │  │
│  │  (control flow) │  │  (quantum ops)   │  │
│  └─────────────────┘  └──────────────────┘  │
└─────────────────────────────────────────────┘
         │ passes transform IR
         ▼
     ftqc-opt
```

A **dialect** bundles:
- **Types** — `!ftqc.logical_qubit`, `!ftqc.syndrome`, `!ftqc.magic_state`
- **Ops** — `ftqc.logical_h`, `ftqc.steane_x_syndrome`, `ftqc.logical_toffoli`
- **Attributes** — `#ftqc.ecc<steane, 3>`, `#ftqc.code_props<7, 1, 3>`
- **Passes** — analysis and transformation passes on the IR

---

## FTQC Type System

### Logical Qubits — parameterized by ECC code and distance

```mlir
!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>     // Steane [[7,1,3]]
!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>    // Surface d=5
!ftqc.logical_qubit<#ftqc.ecc<color_code, 3>> // Triangular color code
```

### Syndrome Types — width depends on code and distance

```mlir
!ftqc.syndrome<6>    // Steane: 3 X-checks + 3 Z-checks
!ftqc.syndrome<16>   // Surface d=3: (d²-1) = 8 X + 8 Z stabilizers
```

### Magic State Types — for non-Clifford gates

```mlir
!ftqc.magic_state          // |T⟩ — enables logical T gate
!ftqc.magic_state<CCZ>     // |CCZ⟩ — enables Toffoli gate
```

### Pauli Frame — deferred classical correction

```mlir
!ftqc.pauli_frame<1>       // tracks Pauli byproducts for 1 qubit
```

---

## Supported Error-Correcting Codes

<div class="columns">
<div>

### Steane \[\[7,1,3\]\]
- **Parameters**: $n=7$, $k=1$, $d=3$
- **Threshold**: $\sim 1\%$
- **Transversal**: H, CNOT, X, Z
- **Magic injection**: T, S via $T^2$
- **Syndrome width**: 6 bits (3X + 3Z)

### Surface Code (distance $d$)
- **Parameters**: $n=d^2$, $k=1$
- **Threshold**: $\sim 1\%$ (high)
- **Logical error**: $p_L \sim (p/p_\text{th})^{(d+1)/2}$
- **Syndrome width**: $2(d^2-1)$ bits

</div>
<div>

### Triangular Color Code
- **Parameters**: $[\![t^2+t+1, 1, t+1]\!]$
- **Transversal**: full Clifford group (incl. S!)
- **Advantage**: S gate without magic injection
- **Status**: stub (lowering not yet implemented)

### Code Properties Attribute
```mlir
#ftqc.code_props<
  n = 7,    // physical qubits
  k = 1,    // logical qubits
  d = 3     // distance
>
```

</div>
</div>

---

## Logical Gate Operations

### Single-qubit Clifford (transversal on all codes)
```mlir
%out = ftqc.logical_h %q   : <<steane, 3>>
%out = ftqc.logical_x %q   : <<steane, 3>>
%out = ftqc.logical_z %q   : <<steane, 3>>
%out = ftqc.logical_s %q   : <<color_code, 3>>   // transversal on color only
```

### Two-qubit gates
```mlir
%c, %t = ftqc.logical_cnot %ctrl, %tgt  : (<<steane,3>>, <<steane,3>>) -> (...)
%a, %b = ftqc.logical_cz   %q0,   %q1   : (<<surface,5>>, <<surface,5>>) -> (...)
```

### Non-Clifford — requires magic state injection
```mlir
%ms  = ftqc.prepare_magic_t : !ftqc.magic_state
%out = ftqc.logical_t %q, %ms : (<<steane,3>>, !ftqc.magic_state) -> <<steane,3>>

%ccz = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
%a, %b, %c = ftqc.logical_toffoli %c0, %c1, %t, %ccz : (...)
```

---

## Syndrome Extraction

After each logical gate, syndrome rounds detect errors before they propagate.

### Steane Method — transversal ancilla measurement
```mlir
// X-type check: detects Z errors
%q2, %xsyn = ftqc.steane_x_syndrome %q : <<steane,3>> -> (<<steane,3>>, <6>)

// Z-type check: detects X errors
%q3, %zsyn = ftqc.steane_z_syndrome %q2 : <<steane,3>> -> (<<steane,3>>, <6>)
```

### Surface Code Rounds
```mlir
%q2, %syn = ftqc.surface_round %q cycles = 3
    : <<surface,5>> -> (<<surface,5>>, <48>)
```

### Error Correction
```mlir
// Inline correction (syndromes decoded immediately)
%corrected = ftqc.correct %q, %xsyn : (<<steane,3>>, <6>) -> <<steane,3>>

// Deferred correction via Pauli frame tracking
%frame = ftqc.create_frame : !ftqc.pauli_frame<1>
%frame2 = ftqc.pauli_frame_update %frame, %xsyn <{qubitIndex = 0}>
              : (!ftqc.pauli_frame<1>, <6>) -> !ftqc.pauli_frame<1>
```

---

## Magic State Distillation

Raw $|T\rangle$ states have high error rate; distillation protocols purify them.

```
        ┌───┐        ┌─────────────┐
 |T⟩  ──┤   │        │             │
 |T⟩  ──┤   │        │  15-to-1    │──▶  |T⟩_clean
  ⋮   ──┤ EC├────────┤ distillation│     (error ε² for input error ε)
 |T⟩  ──┤   │        │ (Reed-Muller│
 |T⟩  ──┤   │        │  7-to-1)    │
        └───┘        └─────────────┘
```

### Distillation Ops
```mlir
%t_clean = ftqc.distill_15to1(%raw_0, ..., %raw_14 : ...) -> !ftqc.magic_state
%t_clean = ftqc.distill_7to1 (%raw_0, ..., %raw_6  : ...) -> !ftqc.magic_state
```

### `--ftqc-magic-distill` pass (fixed — no longer hangs)
Each `prepare_magic_t` op is expanded to 15 noisy input states + one `distill_15to1` op.
The pass uses a **pre-collected snapshot walk** with `IRRewriter` (not greedy driver)
to avoid infinite rewrite loops. Each original `prepare_magic_t` is processed exactly once.

```bash
ftqc-opt --ftqc-transversal-gates --ftqc-magic-distill \
         --ftqc-resource-estimate test/Transforms/Magic/t-gate-inject.mlir
```

---

## Transform Pass Pipeline

```
Source MLIR
    │
    ▼  --ftqc-steane-encode / --ftqc-surface-encode
Encode logical operations into chosen ECC
    │
    ▼  --ftqc-insert-syndrome
Insert X+Z syndrome extraction rounds after each gate (Steane: 2 ops/gate)
    │
    ▼  --ftqc-transversal-gates
Lower S (Steane) → T·T via magic injection; keep S on color code
    │
    ▼  --ftqc-magic-distill  (optional)
Expand prepare_magic_t into 15-to-1 distillation circuit
    │
    ▼  --ftqc-pauli-frame-opt  (optional)
Replace ftqc.correct with deferred Pauli frame tracking
    │
    ▼  --ftqc-resource-estimate
Count T/CNOT/EC rounds; derive physical qubits + logical error rate from ECC type
    │
    ├──▶  --ftqc-lower-to-qasm3    → OpenQASM 3.0 text
    ├──▶  --ftqc-lower-to-stim     → STIM stabilizer circuit + MWPM QEC
    ├──▶  --ftqc-lower-to-qir      → QIR Base Profile LLVM IR
    └──▶  --ftqc-lower-to-iqm-json → IQM Resonance hardware JSON
```

---

## OpenQASM 2/3 Import — `qasm3-import`

Standalone tool that converts OpenQASM 2/3 source to FTQC MLIR logical IR.
All parsing lives in `src/QASM3Importer.h` (pure C++17, no MLIR dependencies).

### Supported gate set

| QASM gate | FTQC MLIR emission |
|-----------|-------------------|
| `h`, `x`, `y`, `z`, `s` | `ftqc.logical_h/x/y/z/s` |
| `t` | `ftqc.prepare_magic_t` + `ftqc.logical_t` |
| `sdg` | three `ftqc.logical_s` (S†= S³) |
| `tdg` | three `ftqc.logical_s` + `ftqc.logical_t` (T†= S³·T) |
| `cx` / `ccx` | `ftqc.logical_cnot` / `prepare_magic_ccz` + `ftqc.logical_toffoli` |
| `cz` | `ftqc.logical_cz` |
| `measure` | `ftqc.logical_measure` |
| `reset` | `ftqc.init_zero` |

### Usage
```bash
# Standalone converter — QASM → FTQC MLIR text on stdout
qasm3-import bell.qasm
qasm3-import --ecc=surface --distance=5 qft3.qasm
qasm3-import --ecc=steane --func-name=my_bell bell.qasm | \
    ftqc-opt --ftqc-insert-syndrome --ftqc-resource-estimate

# Options
qasm3-import [--ecc=<steane|surface|color_code>] [--distance=<N>] \
             [--func-name=<name>] <input.qasm | ->
```

---

## ftqc-opt Native QASM Import

`ftqc-opt` auto-detects `.qasm` / `.qasm2` / `.qasm3` input and transparently runs
the importer before applying the requested pass pipeline.

### QASM-specific options
```
--import-qasm3            Force QASM import (even without .qasm extension)
--qasm-ecc=<kind>         steane (default) | surface | color_code
--qasm-distance=<N>       Code distance (default: 3)
--qasm-func-name=<name>   MLIR function name (default: stem of input filename)
```

### Examples
```bash
# Auto-detect from extension — full FT pipeline on a QASM file:
ftqc-opt bell.qasm --ftqc-insert-syndrome --ftqc-resource-estimate

# Surface code d=5:
ftqc-opt --qasm-ecc=surface --qasm-distance=5 qft3.qasm \
         --ftqc-insert-syndrome --ftqc-resource-estimate

# Force import (no .qasm extension):
ftqc-opt --import-qasm3 --qasm-func-name=adder circuit.txt \
         --ftqc-insert-syndrome

# Full pipeline → IQM JSON:
ftqc-opt bell.qasm \
         --ftqc-insert-syndrome \
         --ftqc-lower-to-iqm-json="output-file=bell.json"
```

All standard `ftqc-opt` passes (`--ftqc-insert-syndrome`, `--ftqc-resource-estimate`,
`--ftqc-lower-to-qasm3`, `--ftqc-lower-to-stim`, etc.) work identically on QASM input.

---

## QASM3 Lowering — `--ftqc-lower-to-qasm3`

Translates FTQC logical IR to OpenQASM 3.0 after all transformation passes.

### Gate mapping

| FTQC op | QASM3 | FTQC op | QASM3 |
|---------|-------|---------|-------|
| `logical_h` | `h` | `logical_cnot` | `cx` |
| `logical_x` | `x` | `logical_cz` | `cz` |
| `logical_y` | `y` | `logical_toffoli` | `ccx` |
| `logical_z` | `z` | `logical_measure` | `bit bN = measure qN;` |
| `logical_s` | `s` | FT-only ops (syndrome, correct, frame) | QASM comment |
| `logical_t` | `t` | | |

### Example — Bell-state QASM3 output

```qasm
OPENQASM 3.0;
include "stdgates.inc";
qubit q0;
qubit q1;
h q0;
// steane_x_syndrome  // steane_z_syndrome
cx q0, q1;
// steane_x_syndrome  // steane_z_syndrome
bit b0 = measure q0;
bit b1 = measure q1;
```

### Options

```bash
--ftqc-lower-to-qasm3                          # write to stderr (default)
--ftqc-lower-to-qasm3="output-file=out.qasm"   # write to file
--ftqc-lower-to-qasm3="include-comments=false" # omit FT-op comments
```

---

## Qiskit Aer Simulation of Generated QASM3 Circuits

QASM3 emitted by `ftqc-opt` is parsed by **Qiskit `qiskit_qasm3_import`** and executed
on `AerSimulator` (4096–8192 shots). All 4 benchmark circuits produce correct results.

| Circuit | Encoding | Phys Q | T gates | EC rounds | Result |
|---------|----------|--------|---------|-----------|--------|
| Bell (H·CNOT) | Steane d=3 | 26 | 0 | 4 | \|00⟩ 51% · \|11⟩ 49% ✓ |
| GHZ-4 (H+3×CNOT) | Steane d=3 | 52 | 0 | 8 | \|0000⟩ 50% · \|1111⟩ 50% ✓ |
| Grover-2 | Surface d=5 | 98 | 0 | 20 | \|11⟩ 100% ✓ |
| QFT-3 | Steane d=3 | 144+105 | 6 | 36 | \|000⟩ 25%·\|001⟩ 50%·\|100⟩ 25% ✓ |

The emitter prepends `include "stdgates.inc";` so that standard gate names
(`h`, `cx`, `cz`, `t`, `s`) are recognized by the Qiskit QASM3 parser.

```bash
# Reproduce (qasm.venv = Qiskit 2.3.0 + Aer 0.17.2 + qiskit_qasm3_import):
qasm.venv/bin/python sim_qasm3.py
```

---

## STIM Stabilizer Circuit Lowering — `--ftqc-lower-to-stim`

Translates FTQC IR to a **STIM** stabilizer circuit for Monte Carlo QEC simulation.

### Physical qubit layout (Steane [[7,1,3]], 13 qubits per logical qubit)

```
Block base + 0..6   → data qubits
Block base + 7,8,9  → X-ancilla (Sx1, Sx2, Sx3)
Block base + 10,11,12 → Z-ancilla (Sz1, Sz2, Sz3)
```

### Steane stabilizer supports (same for X and Z — CSS code)
```
Stab 1: data offsets {0, 2, 4, 6}
Stab 2: data offsets {1, 2, 5, 6}
Stab 3: data offsets {3, 4, 5, 6}
```

### Encoding circuit emitted per `init_zero`
```stim
R 0 1 2 3 4 5 6          # reset all 7 data qubits
H 0 1 3                  # Hadamard on check qubits {0,1,3}
CX 0 2  CX 0 4  CX 0 6  # CNOT fan-out for stabilizer 1
CX 1 2  CX 1 5  CX 1 6  # CNOT fan-out for stabilizer 2
CX 3 4  CX 3 5  CX 3 6  # CNOT fan-out for stabilizer 3
```

---

## STIM — Syndrome Rounds and DETECTOR Records

### Each syndrome round (per logical qubit block)
```stim
R 7 8 9                  # reset X-ancillae
H 7 8 9                  # Hadamard (X-basis measurement)
CX 7 0  7 2  7 4  7 6   # X-stab 1 fan-out
CX 8 1  8 2  8 5  8 6   # X-stab 2 fan-out
CX 9 3  9 4  9 5  9 6   # X-stab 3 fan-out
H 7 8 9
M 7 8 9                  # measure X-ancillae

R 10 11 12               # reset Z-ancillae (no H needed for Z basis)
CX 0 10  2 10  4 10  6 10
CX 1 11  2 11  5 11  6 11
CX 3 12  4 12  5 12  6 12
M 10 11 12               # measure Z-ancillae

DETECTOR rec[-6]         # compare to previous round via rec[-k]
...
DETECTOR rec[-1]
```

DETECTOR offsets use formula: `k = measureCount − (prevRoundMeasureStart + i)`
computed per-block so each detector references the correct history slot.

### Noise model
```stim
DEPOLARIZE1(p)   # after single-qubit gates
DEPOLARIZE2(p)   # after two-qubit gates
X_ERROR(p)       # before each measurement (readout noise)
```
Noise instructions are omitted entirely when `physical-error-rate=0`.

---

## STIM — Final Measurements and Observables

```stim
# Transversal CNOT between logical qubit 0 (ctrl) and qubit 1 (tgt):
CX 0 13  1 14  2 15  3 16  4 17  5 18  6 19

# Final data-qubit measurements:
M 0 1 2 3 4 5 6
OBSERVABLE_INCLUDE(0) rec[-7] rec[-6] rec[-5] rec[-4] rec[-3] rec[-2] rec[-1]

M 13 14 15 16 17 18 19
OBSERVABLE_INCLUDE(1) rec[-7] rec[-6] rec[-5] rec[-4] rec[-3] rec[-2] rec[-1]
```

The logical Z observable is the **XOR (parity) of all 7 data qubit measurements**.
Round-trip circuits (prepare → apply → invert → measure) ensure all STIM observables
are deterministic in the noiseless case, which is required by the STIM DEM format.

### Options
```bash
--ftqc-lower-to-stim                                    # write to stderr
--ftqc-lower-to-stim="syndrome-rounds=3"                # 3 rounds per gate
--ftqc-lower-to-stim="physical-error-rate=1e-3"         # depolarizing noise
--ftqc-lower-to-stim="output-file=bell.stim"            # write to file
--ftqc-lower-to-stim="include-comments=false"           # omit FT-op comments
```

---

## STIM QEC Simulation (`sim_stim.py`)

Sweeps physical error rate $p$ over 18 log-uniform points $[10^{-4}, 1.5\times10^{-2}]$
using **PyMatching** (MWPM decoder), 5000 shots per point.

All benchmark circuits use **round-trip topology** (prepare → gate → inverse → measure)
so noiseless outcome is always $|0\rangle^{\otimes n}$ and STIM observables are deterministic.

### Circuit validation (noiseless, `p=0`)
| Circuit | Physical qubits | Detectors | Observables |
|---------|----------------|-----------|-------------|
| Bell | 26 | 30 | 2 |
| GHZ-4 | 52 | 66 | 4 |
| Grover-2 | 26 | 96 | 2 |
| QFT-3 | 39 | 162 | 0 |

### Logical error rates at $p = 10^{-3}$ (MWPM decoded, per shot)
| Circuit | $p_L$ | Note |
|---------|-------|------|
| Bell (26P) | 9.9% | circuit-level, not per-gate |
| GHZ-4 (52P) | 13.6% | higher due to more qubits |
| Grover-2 (26P) | 27.6% | deep diffusion circuit |
| QFT-3 (39P) | 21.6% | T gates approximated as S (Clifford only) |

```bash
qasm.venv/bin/python sim_stim.py --shots 5000 \
    --output paper_results/stim_results.csv
```

---

## PennyLane Density-Matrix Simulation (`sim_pennylane_qec.py`)

Uses **PennyLane v0.42 `default.mixed`** (density-matrix simulator) with
`DepolarizingChannel` noise on the same Steane-encoded circuits.

**No active QEC decoder** — measures infidelity directly:
$$p_L = 1 - \rho[0,0]$$
where $\rho[0,0]$ is the probability of the ideal $|0\rangle^{\otimes n}$ outcome.

### Setup

```python
# venv: qiree/pen.venv (PennyLane 0.42.0-dev19)
pen.venv/bin/python sim_pennylane_qec.py \
    --outdir paper_results/ \
    --figdir figures/ \
    --stim-csv paper_results/stim_results.csv
```

### Results at $p = 10^{-3}$ (7 physical qubits, 1 logical qubit)

| Benchmark | $p_L$ (PennyLane DM) | $p_L$ (STIM+MWPM) |
|-----------|---------------------|-------------------|
| Bell | 5.3% | 9.9% (26P, active QEC) |
| GHZ | 5.3% | 13.6% (52P, active QEC) |
| Grover | 5.3% | 27.6% (26P, active QEC) |

PennyLane infidelity scales **nearly linearly** with $p$ (~$60p$ at $p=10^{-3}$):
uncorrected errors from all gate locations accumulate without decoding.

---

## PennyLane vs STIM Scalability Comparison

| Property | PennyLane `default.mixed` | STIM + MWPM |
|----------|--------------------------|-------------|
| Simulation method | Exact density matrix | Stabilizer (Clifford) |
| Memory scaling | $O(4^n)$ exponential | $O(n)$ linear |
| Time (14 physical qubits) | **617 s**, 4 GB RAM | < 0.1 s |
| Time (21 physical qubits) | **Out of memory** (64 TB) | < 0.5 s |
| Time (52 physical qubits, 18-pt sweep) | Infeasible | **< 4 s** |
| Active QEC decoder | None (infidelity only) | MWPM (PyMatching) |
| Non-Clifford gates | Exact (T, Toffoli supported) | T approximated as S |
| Max feasible circuit | ~14 physical qubits | >100 physical qubits |

The PennyLane `default.clifford` backend uses STIM internally, giving identical
performance to direct STIM for Clifford-only circuits.

```bash
# Generate all 4 comparison figures (PDF → figures/, PNG → paper_results/):
pen.venv/bin/python sim_pennylane_qec.py --skip-pl \
    --stim-csv paper_results/stim_results.csv \
    --outdir paper_results/ --figdir figures/
```

---

## QIR Lowering — `--ftqc-lower-to-qir`

Translates FTQC IR to **QIR Base Profile v1.0** LLVM IR with full physical-qubit expansion.
Each logical qubit is expanded to a contiguous block of physical qubits.

### Physical expansion — Steane [[7,1,3]]

Each `!ftqc.logical_qubit<#ftqc.ecc<steane,3>>` → **7 physical QIR qubits**.

| FTQC op | QIR expansion |
|---------|--------------|
| `init_zero` | 7 qubits + encoding: `h q[0,1,3]` + 9 CNOT fan-out |
| `init_plus` | same as `init_zero` + transversal H on all 7 |
| `logical_h/x/y/z/s/t` | transversal: 7 single-qubit gates |
| `logical_cnot` | transversal: 7 CNOT pairs (ctrl[i]→tgt[i]) |
| `logical_cz` | transversal: 7 CZ pairs |
| `logical_toffoli` | transversal: 7 CCX triples |
| `logical_measure` | `mz` on all 7; logical outcome = parity (XOR) of 7 bits |

### Physical expansion — Surface code (distance $d$)

Each `!ftqc.logical_qubit<#ftqc.ecc<surface,d>>` → **$d^2$ physical QIR qubits** (row-major).

| FTQC op | QIR expansion |
|---------|--------------|
| `init_zero` | H on first row + vertical CNOTs + horizontal CNOTs |
| `logical_{h,x,y,z,s,t}` | transversal: $d^2$ single-qubit gates |
| `logical_cnot/cz/toffoli` | transversal: $d^2$ two/three-qubit gates |
| `logical_measure` | `mz` on all $d^2$; logical outcome = parity of $d^2$ bits |

---

## QIR — Generated LLVM IR Structure

Output conforms to **QIR Base Profile v1.0** qubit/result pointer convention:
- Index 0 → `ptr null`
- Index N → `ptr inttoptr (i64 N to ptr)`

### Example — Bell state (Steane [[7,1,3]], 14 physical qubits)

```llvm
; attributes #0 = { "entry_point" "required_num_qubits"="14"
;                   "required_num_results"="14" "output_labeling_schema" }

define void @bell_state_ft() #0 {
entry:
  ; Encode logical qubit 0 (qubits 0–6)
  call void @__quantum__qis__h__body(ptr null)           ; h q[0]
  call void @__quantum__qis__h__body(ptr inttoptr(i64 1 to ptr))
  call void @__quantum__qis__h__body(ptr inttoptr(i64 3 to ptr))
  call void @__quantum__qis__cnot__body(ptr null, ptr inttoptr(i64 2 to ptr))
  ; ... 8 more CNOT encoding gates ...

  ; Transversal H on logical qubit 0
  call void @__quantum__qis__h__body(ptr null)           ; h q[0]
  ; ... h q[1] through h q[6] ...

  ; Transversal CNOT (logical ctrl=q0..q6, tgt=q7..q13)
  call void @__quantum__qis__cnot__body(ptr null, ptr inttoptr(i64 7 to ptr))
  ; ... 6 more transversal CNOT gates ...

  ; Measure all 14 physical qubits → parity = logical result
  call void @__quantum__qis__mz__body(ptr null, ptr null)
  ; ...
  ret void
}
```

FT-infrastructure ops (syndrome, correction, Pauli-frame) emit **LLVM IR line comments**
when `--include-comments=true` (default). Unrecognised codes fall back to 1:1 mapping.

---

## QIR — Options and Runtime Declarations

```bash
--ftqc-lower-to-qir                              # write to stderr (default)
--ftqc-lower-to-qir="output-file=out.ll"         # write to .ll file
--ftqc-lower-to-qir="include-comments=false"     # omit FT-op comments
```

The emitter automatically inserts required QIR runtime declarations:

```llvm
declare void @__quantum__rt__initialize(ptr)
declare void @__quantum__qis__h__body(ptr)
declare void @__quantum__qis__cnot__body(ptr, ptr)
declare void @__quantum__qis__mz__body(ptr, ptr writeonly) #1
declare void @__quantum__rt__result_record_output(ptr, ptr)
declare void @__quantum__rt__array_record_output(i64, ptr)
```

The `required_num_qubits` and `required_num_results` entry-point attributes are
computed from the maximum physical qubit/result indices across all functions in the module.

---

## IQM JSON Lowering — `--ftqc-lower-to-iqm-json`

Translates FTQC logical IR to **IQM Resonance** hardware JSON for direct submission
to IQM quantum processors. Native gate set: `prx`, `cz`, `measure`.
Angles are in **half-turns** (1 half-turn = π radians).

### Gate decompositions

| FTQC op | IQM native gates |
|---------|-----------------|
| `logical_h` | `prx(0.5, 0.25)` + `prx(1.0, 0.0)` |
| `logical_x` | `prx(1.0, 0.0)` |
| `logical_y` | `prx(1.0, 0.25)` |
| `logical_z` | `prx(0.5, 0.0)` + `prx(1.0, 0.25)` + `prx(1.5, 0.0)` |
| `logical_s` | `prx(0.5, 0.0)` + `prx(0.5, 0.25)` + `prx(1.5, 0.0)` |
| `logical_t` | `prx(0.5, 0.0)` + `prx(0.25, 0.25)` + `prx(1.5, 0.0)` |
| `logical_cnot` | H(tgt) + `cz[ctrl, tgt]` + H(tgt) |
| `logical_toffoli` | 6-CZ Barenco decomposition (7 T/T† sub-gates) |
| `logical_measure` | `measure` with key `"mQB{n}_{idx}"` |

Qubit names: **QB1, QB2, …, QBN** (data and ancilla share a contiguous counter).

---

## IQM JSON — Syndrome Expansion and Output Format

### Syndrome expansion (default: on)

`steane_x_syndrome` and `steane_z_syndrome` ops are expanded to physical ancilla
sequences (reset → H → CZ fan-out → H → measure) placed inline in the instruction list.
Disable with `expand-syndromes=false` to emit syndrome ops as JSON comments only.

### Example output structure
```json
{
  "name": "bell_state_ft",
  "instructions": [
    {"name": "prx",     "qubits": ["QB1"], "args": {"angle_t": 0.5, "phase_t": 0.25}},
    {"name": "prx",     "qubits": ["QB1"], "args": {"angle_t": 1.0, "phase_t": 0.0}},
    {"name": "cz",      "qubits": ["QB1", "QB2"]},
    {"name": "measure", "qubits": ["QB1"], "key": "mQB1_0"},
    {"name": "measure", "qubits": ["QB2"], "key": "mQB2_0"}
  ]
}
```

The JSON is also stored as the `ftqc.iqm_json` string attribute on the MLIR module.

### Options
```bash
--ftqc-lower-to-iqm-json                              # print JSON to stderr
--ftqc-lower-to-iqm-json="output-file=out.json"       # write to file
--ftqc-lower-to-iqm-json="include-comments=false"     # omit FT-op comments
--ftqc-lower-to-iqm-json="expand-syndromes=false"     # skip syndrome expansion

# Full pipeline: QASM → FT syndrome → IQM JSON
ftqc-opt bell.qasm \
         --ftqc-insert-syndrome \
         --ftqc-lower-to-iqm-json="output-file=bell.json"
```

---

## Physical Initial Layout — `--ftqc-qphys-init-layout`

Maps the logical `id` on every `ftqc.qphys.alloc` produced by
`--ftqc-expand-physical` to a backend hardware-qubit index. The mapping is
read from the JSON file supplied through the `input-file` pass option:

```bash
ftqc-opt physical.mlir \
    --ftqc-qphys-init-layout="input-file=initial-layout.json"
```

```json
{
  "logical_to_hardware_mapping": {
    "0": 5,
    "1": 2,
    "2": 7
  }
}
```

Invoking the pass without `input-file` is a silent no-op. A missing input file,
invalid mapping file, or missing logical-qubit ID causes the pass to fail.

---

## Analysis Passes

### Fault Path Analysis (`--ftqc-fault-path`)
Enumerates leading-order fault paths through the circuit.

```
Fault path analysis for @cnot_fault_paths
  Fault locations: init_zero(0), logical_h(0), logical_cnot(0,1)
  Weight-1 paths:  3 paths → logical error rate O(p)
```

### Threshold Analysis (`--ftqc-threshold`)
Checks whether the physical error rate is below the code threshold.

```mlir
// p = 1e-3 < 1% → circuit is below threshold, no warning
ftqc.init_zero : <<steane, 3>>
```

### Resource Estimation (`--ftqc-resource-estimate`)
Emits a structured annotation on each function:

```mlir
ftqc.resource_annotation {
  tGateCount = 6 : i64,        logicalQubits = 3 : i64,
  cnotCount  = 9 : i64,        physicalQubits = 147 : i64,
  ecRounds   = 36 : i64,       estimatedLogicalErrorRate = 1e-6 : f64
}
```

EC rounds = number of syndrome ops in IR = **2 × gate count** (Steane X+Z per gate).  
Distillation qubits inferred from T-gate count if no explicit `distill_15to1` ops are present.

---

## Full Pipeline Example — 3-qubit QFT

```mlir
func.func @qft3_steane(
    %q0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, ...
) -> (...) {
  // H on q0
  %h0 = ftqc.logical_h %q0 : <<steane, 3>>

  // Controlled-S (Rz(π/2)) via T-gate injection
  %ms0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0a = ftqc.logical_t %h0, %ms0  : (<<steane,3>>, !ftqc.magic_state) -> <<steane,3>>
  %c0a, %t1a = ftqc.logical_cnot %q1, %t0a : ...

  // ... 5 more T gates, 9 CNOTs, 3 Hadamards, 1 SWAP (3 CNOTs)
}
```

**After `--ftqc-insert-syndrome --ftqc-transversal-gates --ftqc-resource-estimate`:**

```
=== FTQC Resource Report ===
Logical T gates     : 6        EC rounds           : 36
Logical CNOT gates  : 9        Distillation rounds : 6
Logical Clifford    : 3        Distillation qubits : 105
Physical total      : 144      Physical error      : 0.001
Est. logical error  : 1.0e-06
```

---

## Grover-FT Resource Report (Surface Code d=5)

```bash
ftqc-opt test/Integration/grover-ft.mlir \
    --ftqc-surface-pipeline  \
    --ftqc-transversal-gates  \
    --ftqc-resource-estimate  \
    --ftqc-lower-to-qasm3
```

```
=== FTQC Resource Report ===
ECC code              : surface code  d=5
Logical qubits        : 2
Logical T gates       : 0
Logical Clifford gates: 12
EC rounds             : 20
Distillation rounds   : 0
---
Physical data qubits  : 50
Physical ancilla      : 48
Physical total        : 98
---
Physical error rate   : 0.001
Est. logical error    : 4.0e-03
```

Physical qubit formulas used by `--ftqc-resource-estimate`:

| Code | Data / logical | Ancilla / logical | $p_\text{th}$ |
|------|----------------|-------------------|--------------|
| Steane [[7,1,3]] | 7 | 6 | 1% |
| Surface code d | $d^2$ | $d^2-1$ | 1% |
| Color code d | $(3d^2+1)/4$ | $d^2/2$ | 0.3% |

---

## Utility Libraries

<div class="columns">
<div>

### Pauli Algebra (`PauliUtils.h`)
- Binary symplectic representation of Pauli operators
- `pauliProduct()`, `pauliWeight()`
- Clifford tableau simulation
- `conjugateByGate()` for H, S, CNOT
- GF(2) Gaussian elimination

### Stabilizer Codes (`StabilizerUtils.h`)
- CSS code structure (X-checks / Z-checks)
- `StabilizerCode` class with parity-check matrices
- Pre-built constants for Steane `[7,1,3]`
- Distance computation, minimum-weight decoder

</div>
<div>

### Syndrome Decoder (`SyndromeDecoder.h`)

Abstract decoder hierarchy:

```
SyndromeDecoder (interface)
├── LookupTableDecoder  ← O(1) for small codes
├── MinWeightDecoder    ← MWPM for surface codes
└── SteaneDecoder       ← Steane-specific
```

Factory: `createDecoder(eccKind, distance)`

### Fault Path Analysis (`FaultPathAnalysis.h`)
- `FaultLocation` — qubit index + gate index + error type
- `FaultPath` — sequence of correlated faults
- `FaultPathAnalysis` — DFS enumeration up to weight $t = \lfloor(d-1)/2\rfloor$

</div>
</div>

---

## Project Architecture

```
qftc/
├── include/FTQC/
│   ├── Dialect/
│   │   ├── FTQCOps.td            ← Op definitions (ODS, 31 ops)
│   │   ├── FTQCTypes.td          ← Type definitions (5 types)
│   │   └── FTQCAttrs.td          ← ECCKind, MagicKind, PauliKind
│   ├── Analysis/
│   │   ├── FaultPathAnalysis.h   ← fault enumeration
│   │   ├── ResourceEstimation.h  ← T/CNOT/EC counts + physical derivation
│   │   └── ThresholdAnalysis.h   ← below-threshold check
│   └── Utils/
│       ├── PauliUtils.h          ← symplectic Pauli algebra
│       ├── StabilizerUtils.h     ← CSS code matrices
│       └── SyndromeDecoder.h     ← decoder hierarchy
├── lib/Transforms/
│   ├── SteaneEncoding.cpp   SurfaceCodeEncoding.cpp  ColorCodeEncoding.cpp
│   ├── SyndromeExtraction.cpp  TransversalGates.cpp
│   ├── MagicStateDistillation.cpp  ErrorCorrection.cpp
│   ├── LowerToQASM3.cpp          ← OpenQASM 3.0 emitter pass
│   ├── LowerToSTIM.cpp           ← STIM stabilizer circuit emitter
│   ├── LowerToQIR.cpp            ← QIR Base Profile LLVM IR emitter
│   ├── LowerToIQMJSON.cpp        ← IQM Resonance hardware JSON emitter
│   └── Passes.cpp                ← pass registration (19 passes)
├── src/
│   ├── QASM3Importer.h           ← shared OpenQASM 2/3 parser (pure C++17)
│   ├── qasm3-import.cpp          ← standalone qasm3-import CLI tool
│   └── ftqc-opt.cpp              ← ftqc-opt driver (native QASM import)
├── sim_qasm3.py                  ← Qiskit Aer: 4 benchmark circuits
├── sim_stim.py                   ← STIM+MWPM: 18-pt p sweep, 5000 shots
├── sim_pennylane_qec.py          ← PennyLane DM vs STIM comparison
├── sim_steane_aer.py             ← Physical-level Steane Aer simulation
├── sim_qsim.py                   ← qsimcirq independent verification
├── plot_stim_results.py          ← 5 STIM result figures (PDF+PNG)
├── paper_results/
│   └── run_paper_results.sh      ← reproduces all paper results end-to-end
├── figures/                      ← PDF vector figures (fig1–fig5, figA–figD)
└── test/                         ← 95 lit tests (all pass)
    ├── Dialect/  Analysis/  Transforms/  Integration/
    ├── Transforms/QASM3/         ← 8 QASM3 lowering tests
    └── Integration/              ← bell.qasm, qft3.qasm, adder.qasm + lit tests
```

---

## Test Suite — 95 Tests, All Pass

| Suite | Tests | Examples |
|-------|-------|---------|
| `Dialect/` | 18 | type round-trip, op parse/print, attribute syntax |
| `Analysis/` | 5 | fault paths, threshold check, resource estimates |
| `Transforms/` | 60 | syndrome insertion, gate decomposition, magic distill |
| `Transforms/QASM3/` | 8 | QASM3 lowering gate names, qubit declarations |
| `Integration/` | 4 | Bell state, GHZ-4, Grover, 3-qubit QFT |
| `Integration/QASM3/` | 3 | bell.qasm, qft3.qasm, adder.qasm → FTQC MLIR |

```bash
# Run all tests (build dir from CMake):
llvm-lit build/test/Transforms \
         build/test/Dialect \
         build/test/Analysis \
         build/test/Integration

# Testing Time: ~0.15s   Passed: 95 (100.00%)
```

Key test patterns validated:
- Type printer abbreviations (`<<steane, 3>>`, `<6>`, `<CCZ>`)
- Full pipeline end-to-end on non-trivial circuits
- `--ftqc-magic-distill` expands `prepare_magic_t` → 15 noisy inputs + `distill_15to1`
- QASM3 gate names, qubit declarations, FT-op comments
- STIM encoding circuit, DETECTOR records, OBSERVABLE_INCLUDE

## Full pipeline — resource report + QASM3 output:
```
ftqc-opt --ftqc-steane-pipeline \
         --ftqc-transversal-gates \
         --ftqc-resource-estimate \
         --ftqc-lower-to-qasm3 \
         test/Integration/qft3-steane.mlir
```
---

## Current Limitations & Future Work

| Feature | Status |
|---------|--------|
| Steane `[[7,1,3]]` full pipeline | ✅ Complete |
| Surface code (any distance) | ✅ Complete |
| OpenQASM 2/3 import (`qasm3-import` + `ftqc-opt` native) | ✅ Complete (h/x/y/z/s/t/sdg/tdg/cx/cz/ccx/measure) |
| QASM3 lowering + Qiskit Aer verification | ✅ Complete (4 benchmarks pass) |
| IQM Resonance JSON lowering | ✅ Complete (prx/cz/measure; syndrome expansion) |
| STIM lowering + MWPM QEC simulation | ✅ Complete (no hang; correct DETECTORs) |
| QIR Base Profile v1.0 lowering | ✅ Complete (Steane 7P + Surface d² expansion) |
| PennyLane DM vs STIM comparison | ✅ Complete (617s/14P vs <4s/52P) |
| `--ftqc-magic-distill` (15-to-1) | ✅ Fixed (snapshot walk; no longer hangs) |
| Resource estimation (ECC-aware) | ✅ Physical qubits + logical error rate |
| Color code transversal S | ✅ Parsed; lowering is stub |
| Fault path analysis | ✅ Weight-1/2 enumeration |
| H·H / X·X canonicalization | ❌ Not implemented |
| Flag-qubit / Shor syndrome (pass) | ❌ Pass always inserts Steane syndrome |
| PennyLane/Catalyst `!quantum.bit` integration | ❌ Future work |
| Threshold-aware scheduling | ❌ Future work |
| Neural-network / adaptive decoder | ❌ Future work |
| Color code encoding (full lowering) | ❌ Stub |

---

## Reproducing All Paper Results

```bash
# From qftc/ directory — full end-to-end reproduction:
bash paper_results/run_paper_results.sh

# Skip slow PennyLane simulation (~10 min):
bash paper_results/run_paper_results.sh --skip-pennylane

# Regenerate figures only (uses existing stim_results.csv):
bash paper_results/run_paper_results.sh --plots-only --skip-pennylane
```

Output layout:
```
paper_results/
  01_build.log  02_lit_tests.log  03_sim_qasm3.log ...  10_latex_pass2.log
  stim_results.csv       pennylane_results.csv
  ftqc-paper.pdf         scale_data.csv  scale_plots.tex
  fig1_pL_vs_p_loglog.png  fig2_suppression.png  ...  figD_summary_panel.png

figures/                 ← PDF vector figures for LaTeX inclusion
  fig1_pL_vs_p_loglog.pdf  fig2_suppression.pdf  fig3_scalability.pdf
  fig4_per_benchmark.pdf   fig5_composite_summary.pdf
  figA_pl_vs_stim_loglog.pdf  figB_all_methods.pdf
  figC_scalability.pdf        figD_summary_panel.pdf
```

---

<!-- _class: lead -->

## Summary

**FTQC** is a full MLIR dialect for fault-tolerant quantum computing:

- **Types**: logical qubits with ECC annotations, syndromes, magic states, Pauli frames
- **Ops**: 31 ops — Clifford+T+Toffoli, syndrome extraction, EC, distillation
- **Passes**: 19 passes — encode → syndrome → transversal → distill → resource → lower
- **Input**: OpenQASM 2/3 via `qasm3-import` tool or native `ftqc-opt` `.qasm` support
- **Back ends**: OpenQASM 3.0, STIM stabilizer circuits, QIR Base Profile LLVM IR, IQM Resonance JSON
- **Simulation**: Qiskit Aer (QASM3) · STIM+MWPM (QEC) · PennyLane DM (comparison)
- **QIR**: physical qubit expansion — Steane (7P/logical), Surface ($d^2$/logical)
- **95 lit tests**, all passing on LLVM/MLIR 22.0.0git

```bash
# QASM import (standalone or via ftqc-opt):
qasm3-import bell.qasm | ftqc-opt --ftqc-insert-syndrome --ftqc-resource-estimate
ftqc-opt bell.qasm --ftqc-insert-syndrome --ftqc-lower-to-iqm-json="output-file=bell.json"

# QASM3 → Qiskit Aer:
qasm.venv/bin/python sim_qasm3.py

# STIM QEC sweep (5000 shots, 18 error rates):
qasm.venv/bin/python sim_stim.py --shots 5000 \
    --output paper_results/stim_results.csv

# PennyLane DM vs STIM comparison figures:
pen.venv/bin/python sim_pennylane_qec.py \
    --stim-csv paper_results/stim_results.csv \
    --outdir paper_results/ --figdir figures/

# QIR generation:
ftqc-opt --ftqc-steane-pipeline --ftqc-lower-to-qir \
    test/Integration/bell-state-ft.mlir
```
python3 -m pip install "iqm-client[qiskit,cirq]" "qrisp[iqm]"

## How to run demo?
```
python3.12 -m venv realqpu
source realqpu/bin/activate
python3.12 -m pip install "qrisp[iqm]" "iqm-client[qiskit,cirq]>=34.0.3" matplotlib
IQM_TOKEN=<token> bash demo.sh
```

