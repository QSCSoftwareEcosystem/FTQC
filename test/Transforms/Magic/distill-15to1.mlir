// RUN: ftqc-opt --ftqc-magic-distill %s | FileCheck %s
// Test: --ftqc-magic-distill expands each prepare_magic_t into a 15-to-1
// distillation circuit (15 noisy inputs → one distill_15to1 op). (test 43)

// CHECK-LABEL: func @t_gate_with_distill
func.func @t_gate_with_distill(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // After distillation: 15 raw noisy states + one distill_15to1 op.
  // CHECK-COUNT-15: ftqc.prepare_magic_t
  // CHECK:          ftqc.distill_15to1
  // CHECK:          ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
