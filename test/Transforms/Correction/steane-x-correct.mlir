// RUN: ftqc-opt %s --ftqc-insert-correction | FileCheck %s
// Test: X error correction insertion after Steane X syndrome. (test 57)

// CHECK-LABEL: func @steane_x_correct
func.func @steane_x_correct() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2, %xsyn = ftqc.steane_x_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  // After --ftqc-insert-correction, a correct op is inserted.
  // CHECK: ftqc.correct
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
