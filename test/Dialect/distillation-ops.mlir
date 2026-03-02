// RUN: ftqc-opt %s | FileCheck %s
// Test: Distillation operations (15-to-1 and 7-to-1).

// CHECK-LABEL: func @distill_15to1
func.func @distill_15to1() -> !ftqc.magic_state {
  %m0  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m1  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m2  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m3  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m4  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m5  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m6  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m7  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m8  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m9  = ftqc.prepare_magic_t : !ftqc.magic_state
  %m10 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m11 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m12 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m13 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m14 = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.distill_15to1
  %out = ftqc.distill_15to1(
      %m0, %m1, %m2, %m3, %m4, %m5, %m6, %m7,
      %m8, %m9, %m10, %m11, %m12, %m13, %m14
      : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state)
      -> !ftqc.magic_state
  func.return %out : !ftqc.magic_state
}

// CHECK-LABEL: func @distill_7to1
func.func @distill_7to1() -> !ftqc.magic_state {
  %m0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %m6 = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.distill_7to1
  %out = ftqc.distill_7to1(
      %m0, %m1, %m2, %m3, %m4, %m5, %m6
      : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state,
        !ftqc.magic_state)
      -> !ftqc.magic_state
  func.return %out : !ftqc.magic_state
}
