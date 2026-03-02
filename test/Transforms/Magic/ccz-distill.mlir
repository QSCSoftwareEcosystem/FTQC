// RUN: ftqc-opt %s | FileCheck %s
// Test: CCZ magic state distillation for Toffoli gate. (test 48)

// CHECK-LABEL: func @ccz_distill_and_toffoli
func.func @ccz_distill_and_toffoli(
    %ctrl0: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %ctrl1: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %tgt:   !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {
  // Prepare noisy CCZ states for distillation.
  %raw0 = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  %raw1 = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  %raw2 = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>

  // CHECK: ftqc.distill_ccz
  %distilled = ftqc.distill_ccz(
      %raw0, %raw1, %raw2
      : !ftqc.magic_state<CCZ>, !ftqc.magic_state<CCZ>,
        !ftqc.magic_state<CCZ>)
      -> !ftqc.magic_state<CCZ>

  // CHECK: ftqc.logical_toffoli
  %c0, %c1, %t2 = ftqc.logical_toffoli %ctrl0, %ctrl1, %tgt, %distilled
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.magic_state<CCZ>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  func.return %c0, %c1, %t2
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
