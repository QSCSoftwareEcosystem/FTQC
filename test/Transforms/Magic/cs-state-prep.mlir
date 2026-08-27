// RUN: ftqc-opt %s | FileCheck %s
// Test: CS and CCZ magic state preparation. (test 42)

// CHECK-LABEL: func @prepare_ccz_state
func.func @prepare_ccz_state() -> !ftqc.magic_state<CCZ> {
  // CHECK: ftqc.prepare_magic_ccz : <CCZ>
  %ms = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  func.return %ms : !ftqc.magic_state<CCZ>
}

// CHECK-LABEL: func @toffoli_via_ccz
func.func @toffoli_via_ccz(
    %ctrl0: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %ctrl1: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %tgt:   !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  %ms = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  // CHECK: ftqc.logical_toffoli
  %c0, %c1, %t2 = ftqc.logical_toffoli %ctrl0, %ctrl1, %tgt, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.magic_state<CCZ>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  func.return %c0, %c1, %t2
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
