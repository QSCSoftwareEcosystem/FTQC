// RUN: ftqc-opt %s | FileCheck %s
// Test: T magic state preparation. (test 41)
// |T⟩ = T|+⟩ = (|0⟩ + e^{iπ/4}|1⟩)/√2

// CHECK-LABEL: func @prepare_t_state
func.func @prepare_t_state() -> !ftqc.magic_state {
  // CHECK: ftqc.prepare_magic_t : !ftqc.magic_state
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  func.return %ms : !ftqc.magic_state
}

// CHECK-LABEL: func @prepare_multiple_t
func.func @prepare_multiple_t() {
  %m0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m2 = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK-COUNT-3: ftqc.prepare_magic_t
  func.return
}
