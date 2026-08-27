// RUN: ftqc-opt %s | FileCheck %s
// Test: T gate via magic state injection (non-transversal). (test 69)

// CHECK-LABEL: func @t_via_magic_steane
func.func @t_via_magic_steane(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.prepare_magic_t
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @t_via_magic_surface
func.func @t_via_magic_surface(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>> {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
}
