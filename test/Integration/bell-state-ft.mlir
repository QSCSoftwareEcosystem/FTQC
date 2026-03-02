// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate \
// RUN:   | FileCheck %s
// Test: End-to-end Bell state preparation with Steane [[7,1,3]] encoding. (test 76)
//
// Prepares the Bell state (|00> + |11>)/√2 using a fault-tolerant pipeline:
//   1. Encode physical qubits to Steane logical qubits
//   2. Apply logical H to first qubit
//   3. Apply logical CNOT
//   4. Measure both qubits
// The pipeline inserts syndrome extraction and correction rounds automatically.

// CHECK-LABEL: func @bell_state_ft
// CHECK:       ftqc.resource_annotation
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.logical_h
// CHECK:       ftqc.logical_cnot

func.func @bell_state_ft() -> (i1, i1) {
  // Allocate two Steane-encoded logical qubits
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // Logical H on qubit 0: |0> -> (|0> + |1>)/√2
  %h0 = ftqc.logical_h %lq0
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // Logical CNOT creates entanglement: |+>|0> -> (|00> + |11>)/√2
  %c, %t = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // Measure both qubits: should give correlated outcomes (00 or 11)
  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1

  func.return %b0, %b1 : i1, i1
}
