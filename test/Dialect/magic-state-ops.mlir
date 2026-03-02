// RUN: ftqc-opt %s | FileCheck %s
// Test: Magic state prepare operations.

// CHECK-LABEL: func @prepare_t
func.func @prepare_t() {
  // CHECK: ftqc.prepare_magic_t : !ftqc.magic_state
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  func.return
}

// CHECK-LABEL: func @prepare_ccz
func.func @prepare_ccz() {
  // CHECK: ftqc.prepare_magic_ccz : <CCZ>
  %ms = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  func.return
}

// CHECK-LABEL: func @logical_s
func.func @logical_s(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // CHECK: ftqc.logical_s
  %out = ftqc.logical_s %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @logical_t_with_magic
func.func @logical_t_with_magic(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
