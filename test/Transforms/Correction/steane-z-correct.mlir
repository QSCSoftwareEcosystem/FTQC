// RUN: ftqc-opt %s --ftqc-insert-correction | FileCheck %s
// Test: Z error correction insertion after Steane Z syndrome. (test 58)

// CHECK-LABEL: func @steane_z_correct
func.func @steane_z_correct() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2, %zsyn = ftqc.steane_z_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  // CHECK: ftqc.correct
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
