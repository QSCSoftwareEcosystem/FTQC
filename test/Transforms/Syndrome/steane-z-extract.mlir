// RUN: ftqc-opt %s --ftqc-insert-syndrome | FileCheck %s
// Test: Steane method Z syndrome insertion. (test 50)

// CHECK-LABEL: func @steane_z_insert
func.func @steane_z_insert() {
  %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %z  = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.steane_z_syndrome
  %bit = ftqc.logical_measure %z
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return
}
