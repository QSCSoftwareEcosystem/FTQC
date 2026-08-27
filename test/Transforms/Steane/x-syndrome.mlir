// RUN: ftqc-opt %s | FileCheck %s
// Test: X stabilizer syndrome extraction for Steane code. (test 25)
//
// The Steane X syndrome is measured by the Z stabilizers.
// For a Z error on qubit q, the syndrome gives the column q of H.

// CHECK-LABEL: func @steane_x_syndrome_clean
func.func @steane_x_syndrome_clean() {
  // |0⟩_L has no X error → syndrome should be 000.
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.steane_x_syndrome
  %lq2, %xsyn = ftqc.steane_x_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  // CHECK: <3>)
  ftqc.dealloc %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}

// CHECK-LABEL: func @steane_x_syndrome_after_h
func.func @steane_x_syndrome_after_h() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // After H: |0⟩ → |+⟩_L, which has X errors detectable by Z stabilizers.
  // CHECK: ftqc.steane_x_syndrome
  %h2, %xsyn = ftqc.steane_x_syndrome %h
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<3>)
  ftqc.dealloc %h2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
