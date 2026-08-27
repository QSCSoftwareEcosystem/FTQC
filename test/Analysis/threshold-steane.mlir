// RUN: ftqc-opt %s --ftqc-threshold | FileCheck %s
// Test: Threshold analysis for Steane code below threshold. (test 73)

// CHECK-LABEL: func @below_threshold
func.func @below_threshold() {
  // p = 1e-3 < 1% threshold → should pass without warning.
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h  = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %h : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
// CHECK-NOT: warning: circuit may be above
