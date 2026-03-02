// RUN: ftqc-opt %s | FileCheck %s
// Test: Shor cat-state ancilla syndrome extraction. (test 28)

// CHECK-LABEL: func @shor_syndrome_steane
func.func @shor_syndrome_steane() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // Shor method: prepare cat state, couple, verify, measure.
  // CHECK: ftqc.shor_syndrome
  %lq2, %syn = ftqc.shor_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %lq3 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}
