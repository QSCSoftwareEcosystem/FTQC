// RUN: ftqc-opt %s --ftqc-insert-syndrome | FileCheck %s
// Test: Shor cat-state syndrome extraction insertion. (test 54)
// Note: --ftqc-insert-syndrome currently inserts Steane-style syndrome ops.

// CHECK-LABEL: func @shor_insertion
func.func @shor_insertion() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %z = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.steane_x_syndrome
  %bit = ftqc.logical_measure %z
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  func.return
}
