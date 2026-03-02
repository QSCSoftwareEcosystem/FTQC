// RUN: ftqc-opt %s --ftqc-steane-encode | FileCheck %s
// Test: Single-qubit encode from quantum.custom to ftqc.logical operations.
// (test 19)

// Simulated input: a function with a single-qubit H gate.
// CHECK-LABEL: func @single_h_encoded
func.func @single_h_encoded() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %out = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_h
  // CHECK: <<steane, 3>>
  %bit = ftqc.logical_measure %out
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  func.return
}
