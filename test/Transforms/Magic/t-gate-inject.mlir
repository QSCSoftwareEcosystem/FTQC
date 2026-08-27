// RUN: ftqc-opt %s | FileCheck %s
// Test: T gate injection using pre-prepared magic state. (test 46)

// CHECK-LABEL: func @t_inject_chain
func.func @t_inject_chain(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %ms0 = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.logical_t
  %lq1 = ftqc.logical_t %lq, %ms0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %ms1 = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.logical_t
  %lq2 = ftqc.logical_t %lq1, %ms1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %lq2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
