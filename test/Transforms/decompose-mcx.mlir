// RUN: ftqc-opt %s --ftqc-decompose-mcx | FileCheck %s
//
// --ftqc-decompose-mcx rewrites logical_mcx into a Toffoli network with clean
// ancilla (Barenco V-chain).  A 4-controlled X uses (n-2)=2 ancilla and
// 2(n-1)-1 = 5 Toffoli gates; the results are returned so the (pure) rewrite
// is not dead-code-eliminated.

// CHECK-LABEL: func @c4x
// CHECK-NOT: ftqc.logical_mcx
// CHECK-COUNT-2: ftqc.init_zero
// CHECK-COUNT-5: ftqc.logical_toffoli
// CHECK-COUNT-2: ftqc.dealloc
func.func @c4x(
    %c0: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %c1: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %c2: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %c3: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %t: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {
  %o:5 = ftqc.logical_mcx [%c0, %c1, %c2, %c3], %t
      : [!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>],
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> [!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>],
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  return %o#0, %o#1, %o#2, %o#3, %o#4
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
