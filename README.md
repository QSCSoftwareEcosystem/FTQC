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
// High-fidelity magic state after distillation (future pass output)
%t_clean = ftqc.distill_15to1 %raw_t : (!ftqc.magic_state) -> !ftqc.magic_state
%t_clean = ftqc.distill_7to1  %raw_t : (!ftqc.magic_state) -> !ftqc.magic_state
```

> **Status**: `--ftqc-magic-distill` pass infrastructure exists; full protocol implementation is in progress.

---

## Transform Pass Pipeline

```
Source MLIR
    │
    ▼  --ftqc-steane-encode / --ftqc-surface-encode
Encode logical operations into chosen ECC
    │
    ▼  --ftqc-insert-syndrome
Insert syndrome extraction rounds after each gate
    │
    ▼  --ftqc-transversal-gates
Lower S (Steane) → T·T via magic injection; keep S on color code
    │
    ▼  --ftqc-magic-distill  (optional)
Wrap magic state prep in distillation protocol
    │
    ▼  --ftqc-pauli-frame-opt  (optional)
Replace ftqc.correct with deferred Pauli frame tracking
    │
    ▼  --ftqc-resource-estimate
Annotate with T-count, CNOT-count, EC rounds, physical qubit estimate
    │
    ▼  --ftqc-lower-to-qasm3
Emit OpenQASM 3.0 text (stderr or file) + attach as ftqc.qasm3 module attr
    │
    ▼
Annotated MLIR + resource report + OpenQASM 3.0 output
```

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

### Example — Grover 2-qubit oracle (surface d=5, after full pipeline)

```qasm
OPENQASM 3.0;
include "stdgates.inc";

// function @grover_ft
qubit q0;
qubit q1;
h q0;
// steane_x_syndrome
// correct
// steane_z_syndrome
// correct
h q1;
// ...syndrome rounds after every gate...
cz q0, q1;  h q0;  h q1;  x q0;  x q1;
cz q0, q1;  x q0;  x q1;  h q0;  h q1;
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

QASM3 emitted by `ftqc-opt` is fed directly into **Qiskit Aer 0.17** via the
`qiskit_qasm3_import` package. All 4 benchmark circuits produce correct
measurement distributions (4096–8192 shots, `p_phys = 10^{-3}`).

| Circuit | Encoding | Phys Q | Logical Q | T gates | EC rounds | Result |
|---------|----------|--------|-----------|---------|-----------|--------|
| Bell state (H·CNOT) | Steane d=3 | **26** | 2 | 0 | 0 | \|00⟩ 51% · \|11⟩ 49% ✓ |
| GHZ-4 (H+3 CNOTs) | Steane d=3 | **52** | 4 | 0 | 0 | \|0000⟩ 49% · \|1111⟩ 51% ✓ |
| Grover (oracle \|11⟩) | Surface d=5 | **98** | 2 | 0 | 20 | \|11⟩ 100% ✓ |
| QFT-3 (T-approx) | Steane d=3 | **144** + 105 distil | 3 | 6 | 0 | \|000⟩ 25% · \|001⟩ 50% · \|100⟩ 25% ✓ |

Physical qubit formulas used by `--ftqc-resource-estimate`:

| Code | Data qubits / logical | Ancilla / logical | Threshold |
|------|-----------------------|-------------------|-----------|
| Steane [[7,1,3]] | 7 | 6 | ~1% |
| Surface code d | d² | d²−1 | ~1% |
| Color code d | (3d²+1)/4 | d²/2 | ~0.3% |

```bash
# Reproduce:
qasm.venv/bin/python sim_qasm3.py
```

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
  ecRounds   = 36 : i64,       estimatedLogicalErrorRate = 0.0 : f64
}
```

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
```

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
│   │   ├── FTQCOps.td       ← Op definitions (ODS)
│   │   ├── FTQCTypes.td     ← Type definitions
│   │   └── FTQCAttrs.td     ← ECCKind, MagicKind, PauliKind
│   ├── Analysis/
│   │   ├── FaultPathAnalysis.h    ← fault enumeration
│   │   ├── ResourceEstimation.h   ← T/CNOT/EC counts
│   │   └── ThresholdAnalysis.h    ← below-threshold check
│   └── Utils/
│       ├── PauliUtils.h     ← symplectic Pauli algebra
│       ├── StabilizerUtils.h← CSS code matrices
│       └── SyndromeDecoder.h← decoder hierarchy
├── lib/Transforms/
│   ├── SteaneEncoding.cpp   SurfaceCodeEncoding.cpp  ColorCodeEncoding.cpp
│   ├── SyndromeExtraction.cpp  TransversalGates.cpp
│   ├── MagicStateDistillation.cpp  ErrorCorrection.cpp
│   ├── LowerToQASM3.cpp     ← QASM3 emitter pass
│   └── Passes.cpp           ← pass registration
├── sim_qasm3.py             ← Qiskit Aer simulation of 4 benchmark circuits
└── test/                    ← 87 lit tests (all pass)
    ├── Dialect/  Analysis/  Transforms/  Integration/
    └── Transforms/QASM3/    ← 8 QASM3 lowering tests
```

---

## Test Suite — 87 Tests, All Pass

| Suite | Tests | Examples |
|-------|-------|---------|
| `Dialect/` | 17 | type round-trip, op parse/print, attribute syntax |
| `Analysis/` | 6 | fault paths, threshold check, resource estimates |
| `Transforms/` | 52 | syndrome insertion, gate decomposition, EC rounds |
| `Transforms/QASM3/` | 8 | gate mapping, comments, options, full pipeline |
| `Integration/` | 4 | Bell state, GHZ-4, Grover, 3-qubit QFT |

```bash
# Run all tests
llvm-lit build.cmake/test/Transforms \
         build.cmake/test/Dialect \
         build.cmake/test/Analysis \
         build.cmake/test/Integration

# Testing Time: 0.13s   Passed: 87 (100.00%)
```

Key test patterns validated:
- Type printer abbreviations (`<<steane, 3>>`, `<6>`, `<CCZ>`)
- Full pipeline end-to-end on non-trivial circuits
- Syndrome insertion after every logical gate
- Pauli frame deferred correction
- QASM3 gate names, qubit declarations, FT-op comments
- `include-comments=false` option suppresses syndrome/correct lines

---

## Current Limitations & Future Work

| Feature | Status |
|---------|--------|
| Steane `[[7,1,3]]` full pipeline | ✅ Complete |
| Surface code (any distance) | ✅ Complete |
| Color code transversal S | ✅ Parsed; lowering is stub |
| Fault path analysis | ✅ Weight-1/2 enumeration |
| Resource estimation | ✅ T / CNOT / EC / physical qubits (ECC-aware formulas) |
| QASM3 lowering (`--ftqc-lower-to-qasm3`) | ✅ Complete; verified via Qiskit Aer simulation |
| 15-to-1 distillation pass | 🚧 Infrastructure present; hangs on large circuits |
| H·H / X·X canonicalization | ❌ Not implemented |
| Flag-qubit syndrome extraction | ❌ Pass inserts Steane syndrome only |
| Shor cat-state extraction | ❌ Same |
| `--ftqc-lower-to-quantum` | 🚧 Lowering to physical gates |
| Threshold-aware scheduling | ❌ Future work |

---

<!-- _class: lead -->

## Summary

**FTQC** is a full MLIR dialect for fault-tolerant quantum computing:

- **Types**: logical qubits with ECC annotations, syndromes, magic states
- **Ops**: Clifford + T gate set, syndrome extraction, error correction, distillation
- **Passes**: encode → insert-syndrome → transversal-gates → resource-estimate → lower-to-qasm3
- **Analysis**: fault paths, threshold check, physical qubit resource report (ECC-aware)
- **Simulation**: Qiskit Aer verified on Bell, GHZ-4, Grover, QFT-3 — all pass
- **87 lit tests**, all passing on LLVM/MLIR 22.0.0git

**Built with**: MLIR ODS (TableGen), C++17, LLVM 22, lit + FileCheck

```bash
# Full pipeline — resource report + QASM3 output:
ftqc-opt --ftqc-steane-pipeline \
         --ftqc-transversal-gates \
         --ftqc-resource-estimate \
         --ftqc-lower-to-qasm3 \
         test/Integration/qft3-steane.mlir

# Simulate QASM3 output with Qiskit Aer (all 4 circuits):
qasm.venv/bin/python sim_qasm3.py
```
