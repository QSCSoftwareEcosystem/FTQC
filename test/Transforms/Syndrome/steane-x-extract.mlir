// RUN: ftqc-opt %s --ftqc-insert-syndrome | FileCheck %s
// Test: Steane method X syndrome insertion pass. (test 49)

// CHECK-LABEL: func @steane_x_insert
func.func @steane_x_insert() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %h  = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // After pass, syndrome extraction should be inserted after H.
  // CHECK: ftqc.steane_x_syndrome
  %bit = ftqc.logical_measure %h
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  func.return
}
