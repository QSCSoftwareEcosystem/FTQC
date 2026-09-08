// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: S gate via magic state injection on a code without transversal S. (test 47)
// S is NOT transversal on the surface code, so --ftqc-transversal-gates
// decomposes S = T·T via two magic-state injections.  (On the Steane code S is
// transversal and is preserved; see transversal-s-color.mlir.)

// CHECK-LABEL: func @s_gate_surface
func.func @s_gate_surface(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<surface, 3>> {
  // S = T^2 on the surface code is decomposed into 2 T gate injections.
  // CHECK: ftqc.prepare_magic_t
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
}

// CHECK-LABEL: func @s_gate_color_transversal
func.func @s_gate_color_transversal(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>> {
  // S is transversal on color codes, no decomposition needed.
  // CHECK: ftqc.logical_s
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<color_code, 3>>
}
