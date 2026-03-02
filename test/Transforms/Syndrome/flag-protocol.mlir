// RUN: ftqc-opt %s --ftqc-insert-syndrome | FileCheck %s
// Test: Flag-qubit syndrome extraction (Chao-Reichardt). (test 53)
// Note: --ftqc-insert-syndrome currently inserts Steane-style syndrome ops.

// CHECK-LABEL: func @flag_insertion
func.func @flag_insertion() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %h = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.steane_x_syndrome
  %bit = ftqc.logical_measure %h
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  func.return
}
