// RUN: ftqc-opt %s | FileCheck %s
// Test: Error correction on surface code after syndrome measurement. (test 59)

// CHECK-LABEL: func @surface_correct
func.func @surface_correct() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  %lq2, %syn = ftqc.surface_round %lq
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<16>)
  // CHECK: ftqc.correct
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.syndrome<16>)
      -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %lq3 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
