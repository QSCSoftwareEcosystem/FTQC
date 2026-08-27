// RUN: ftqc-opt %s | FileCheck %s
// Test: Erasure error correction (qubit loss detected). (test 64)
// Erasure errors have known location; correction is simpler.

// CHECK-LABEL: func @erasure_correct
func.func @erasure_correct() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // Extract syndrome to detect the erasure location.
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  // CHECK: ftqc.correct
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %lq3 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
