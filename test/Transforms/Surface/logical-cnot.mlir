// RUN: ftqc-opt %s | FileCheck %s
// Test: Transversal CNOT on surface code. (test 37)

// CHECK-LABEL: func @surface_cnot
func.func @surface_cnot() {
  %ctrl = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  %tgt  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  // CHECK: ftqc.logical_cnot
  %c2, %t2 = ftqc.logical_cnot %ctrl, %tgt
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>)
  ftqc.dealloc %c2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 3>>
  func.return
}
