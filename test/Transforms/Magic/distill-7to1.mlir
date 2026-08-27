// RUN: ftqc-opt --ftqc-magic-distill %s | FileCheck %s
// Test: --ftqc-magic-distill (default 15to1 protocol) on a single T gate.
// Verifies the pass no longer hangs and emits the correct distillation IR. (test 44)

// CHECK-LABEL: func @t_gate_7to1
func.func @t_gate_7to1(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // prepare_magic_t is replaced by 15 raw noisy states + distill_15to1.
  // CHECK-COUNT-15: ftqc.prepare_magic_t
  // CHECK:          ftqc.distill_15to1
  // CHECK:          ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
