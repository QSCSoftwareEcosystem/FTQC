// RUN: ftqc-opt %s --ftqc-resource-estimate 2>&1 | FileCheck %s
//
// Two logical Toffoli gates: each is charged 7 T (standard Clifford+T cost) and
// one CCZ magic state, so the reported T-count is 2*7 = 14 and a CCZ
// distillation factory is added to the physical footprint (issue #8).
//
// CHECK: Toffoli (CCZ) gates   : 2
// CHECK: T-count (incl. Toffoli@7T): 14
// CHECK: tGateCount = 14
func.func @two_toffoli(%a: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, %b: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, %c: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {
  %m0 = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  %a1, %b1, %c1 = ftqc.logical_toffoli %a, %b, %c, %m0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state<CCZ>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %m1 = ftqc.prepare_magic_ccz : !ftqc.magic_state<CCZ>
  %a2, %b2, %c2 = ftqc.logical_toffoli %a1, %b1, %c1, %m1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state<CCZ>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  return %a2, %b2, %c2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
