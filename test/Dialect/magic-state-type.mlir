// RUN: ftqc-opt %s | FileCheck %s
// Test: MagicStateType round-trip.

// CHECK-LABEL: func @magic_t_state
func.func @magic_t_state() {
  // CHECK: ftqc.prepare_magic_t : !ftqc.magic_state
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  ftqc.dealloc %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return
}

// CHECK-LABEL: func @magic_ccz_state
func.func @magic_ccz_state() {
  // CHECK: ftqc.prepare_magic_ccz : <CCZ>
  %ms = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  func.return
}
