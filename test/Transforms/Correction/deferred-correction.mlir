// RUN: ftqc-opt %s | FileCheck %s
// Test: Deferred correction via Pauli frame tracking. (test 62)

// CHECK-LABEL: func @deferred_correction_chain
func.func @deferred_correction_chain() -> i1 {
  %frame = ftqc.create_frame : !ftqc.pauli_frame<1>
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // Gate 1: H
  %h = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h2, %syn0 = ftqc.extract_syndrome %h
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
  // CHECK: ftqc.pauli_frame_update
  %frame1 = ftqc.pauli_frame_update %frame, %syn0, 0
      : !ftqc.pauli_frame<1>, !ftqc.syndrome<6> -> !ftqc.pauli_frame<1>

  // Measure: apply frame and measure.
  // CHECK: ftqc.logical_measure
  %bit = ftqc.logical_measure %h2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return %bit : i1
}
