// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: S gate via magic state injection (for Steane/surface code). (test 47)
// After --ftqc-transversal-gates, S on Steane code → T·T.

// CHECK-LABEL: func @s_gate_steane
func.func @s_gate_steane(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // S = T^2 on Steane code is decomposed into 2 T gate injections.
  // CHECK: ftqc.prepare_magic_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @s_gate_color_transversal
func.func @s_gate_color_transversal(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>> {
  // S is transversal on color codes, no decomposition needed.
  // CHECK: ftqc.logical_s
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<color_code, dist = 3>>
}
