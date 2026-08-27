// RUN: ftqc-opt %s | FileCheck %s
// Test: Logical H gate on Steane code. (test 21)
// H·H = I (folding check).

// CHECK-LABEL: func @logical_h_self_inverse
func.func @logical_h_self_inverse(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %h1 = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_h
  // CHECK: ftqc.logical_h
  // (H·H canonicalization not yet implemented; both gates are preserved)
  %h2 = ftqc.logical_h %h1 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %h2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @logical_h_on_plus
func.func @logical_h_on_plus() -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // |+⟩ = H|0⟩, so H|+⟩ = |0⟩
  %plus = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_h
  %zero = ftqc.logical_h %plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
