// RUN: ftqc-opt %s | FileCheck %s
// Test: Logical Z gate (phase flip). (test 23)

// CHECK-LABEL: func @logical_z_phase_flip
func.func @logical_z_phase_flip() -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  %lq = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_z
  %flipped = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %flipped : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @logical_z_z_is_identity
func.func @logical_z_z_is_identity(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  %z1 = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %z2 = ftqc.logical_z %z1 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %z2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
