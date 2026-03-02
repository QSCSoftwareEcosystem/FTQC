//===- ghz4-steane.mlir - 4-qubit GHZ state with Steane [[7,1,3]] --------===//
//
// Input circuit for the 4-qubit GHZ state |Ψ⟩ = (|0000⟩ + |1111⟩) / √2
// encoded in the Steane [[7,1,3]] code with full fault-tolerant syndrome
// extraction and error correction.
//
// Logical circuit
// ───────────────
//   q0 ──H──●──────────── M
//           │
//   q1 ──────X──●──────── M
//              │
//   q2 ──────────X──●──── M
//                   │
//   q3 ──────────────X─── M
//
// FT lowering (this test)
// ───────────────────────
// Each gate is followed by Steane-method syndrome extraction:
//   steane_x_syndrome  (X-stabiliser check using transversal |+⟩ ancilla)
//   steane_z_syndrome  (Z-stabiliser check using transversal |0⟩ ancilla)
//
// Gate count / resource summary
// ──────────────────────────────
//   Logical qubits : 4   (4 × [[7,1,3]] = 28 physical data qubits at d=3)
//   Logical H      : 1
//   Logical CNOTs  : 3
//   EC rounds      : 8   (1 H + 3 CNOTs = 4 gates, each contributes 2 rounds)
//
// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate \
// RUN:   | FileCheck %s

// ── Resource annotation ────────────────────────────────────────────────────
// CHECK-LABEL: func @ghz4_steane
// CHECK:       ftqc.resource_annotation
// CHECK-SAME:  cnotCount = 3 : i64
// CHECK-SAME:  ecRounds = 8 : i64
// CHECK-SAME:  logicalQubits = 4 : i64
// CHECK-SAME:  tGateCount = 0 : i64

// ── Qubit initialisation ───────────────────────────────────────────────────
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.init_zero

// ── H on q0 followed by Steane syndrome ────────────────────────────────────
// CHECK:       ftqc.logical_h
// CHECK-NEXT:  ftqc.steane_x_syndrome
// CHECK-NEXT:  ftqc.steane_z_syndrome

// ── CNOT(q0 → q1) followed by Steane syndrome ──────────────────────────────
// CHECK:       ftqc.logical_cnot
// CHECK-NEXT:  ftqc.steane_x_syndrome
// CHECK-NEXT:  ftqc.steane_z_syndrome

// ── CNOT(q1 → q2) followed by Steane syndrome ──────────────────────────────
// CHECK:       ftqc.logical_cnot
// CHECK-NEXT:  ftqc.steane_x_syndrome
// CHECK-NEXT:  ftqc.steane_z_syndrome

// ── CNOT(q2 → q3) followed by Steane syndrome ──────────────────────────────
// CHECK:       ftqc.logical_cnot
// CHECK-NEXT:  ftqc.steane_x_syndrome
// CHECK-NEXT:  ftqc.steane_z_syndrome

//===----------------------------------------------------------------------===//
// Input: 4-qubit GHZ logical circuit (Steane [[7,1,3]] logical qubits)
//===----------------------------------------------------------------------===//

func.func @ghz4_steane() -> (i1, i1, i1, i1) {
  // ── Allocate four Steane [[7,1,3]] logical qubits ──────────────────────
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq2 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq3 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // ── Step 1: H on q0 ────────────────────────────────────────────────────
  // q0: |0⟩ → (|0⟩ + |1⟩) / √2
  %h0 = ftqc.logical_h %lq0
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // ── Step 2: CNOT(q0 → q1) ──────────────────────────────────────────────
  // State: (|00⟩ + |11⟩) / √2  ⊗ |0⟩|0⟩
  %c0, %t1 = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // ── Step 3: CNOT(q1 → q2) ──────────────────────────────────────────────
  // State: (|000⟩ + |111⟩) / √2  ⊗ |0⟩
  %c1, %t2 = ftqc.logical_cnot %t1, %lq2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // ── Step 4: CNOT(q2 → q3) ──────────────────────────────────────────────
  // State: (|0000⟩ + |1111⟩) / √2
  %c2, %t3 = ftqc.logical_cnot %t2, %lq3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // ── Measure all four qubits ─────────────────────────────────────────────
  // Outcomes are perfectly correlated: (0,0,0,0) or (1,1,1,1)
  %b0 = ftqc.logical_measure %c0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b1 = ftqc.logical_measure %c1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b2 = ftqc.logical_measure %t2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b3 = ftqc.logical_measure %t3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1

  func.return %b0, %b1, %b2, %b3 : i1, i1, i1, i1
}
