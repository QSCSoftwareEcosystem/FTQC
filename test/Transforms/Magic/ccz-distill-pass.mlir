// RUN: ftqc-opt %s --ftqc-magic-distill | FileCheck %s
//
// --ftqc-magic-distill expands each prepare_magic_ccz into a small CCZ factory
// (Haah et al.): 8 noisy CCZ inputs + one distill_ccz (issue #6).
//
// CHECK-LABEL: func @ccz_factory
// CHECK-COUNT-8: ftqc.prepare_magic_ccz
// CHECK: ftqc.distill_ccz
func.func @ccz_factory(%a: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, %b: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, %c: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {
  %m = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  %a1, %b1, %c1 = ftqc.logical_toffoli %a, %b, %c, %m
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state<CCZ>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  return %a1, %b1, %c1 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
