// RUN: ftqc-opt %s | FileCheck %s
// Test: 7-to-1 T state distillation (parse + operand check). (test 44)
// Note: --ftqc-magic-distill pass is a stub and hangs on this circuit;
//       this test validates the source IR is parseable and well-formed.

// CHECK-LABEL: func @t_gate_7to1
func.func @t_gate_7to1(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.prepare_magic_t
  // CHECK: ftqc.logical_t
  %out = ftqc.logical_t %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
