// RUN: ftqc-opt %s | FileCheck %s
// Test: Logical X gate (bit flip). (test 22)

// CHECK-LABEL: func @logical_x_flip
func.func @logical_x_flip() -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_x
  %flipped = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %flipped : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @logical_x_x_is_identity
func.func @logical_x_x_is_identity(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // CHECK: ftqc.logical_x
  %x1 = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_x
  %x2 = ftqc.logical_x %x1 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // (X·X canonicalization not yet implemented; both gates are preserved)
  func.return %x2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
