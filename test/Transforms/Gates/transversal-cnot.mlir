// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal CNOT gate verification. (test 67)

// CHECK-LABEL: func @transversal_cnot
func.func @transversal_cnot(
    %ctrl: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %tgt:  !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  // CNOT is transversal on Steane code.
  // CHECK: ftqc.logical_cnot
  %c2, %t2 = ftqc.logical_cnot %ctrl, %tgt
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  func.return %c2, %t2
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
