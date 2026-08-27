// RUN: ftqc-opt %s --ftqc-transversal-gates | FileCheck %s
// Test: Transversal CNOT on Steane code. (test 24)

// CHECK-LABEL: func @logical_cnot_bell_prep
func.func @logical_cnot_bell_prep()
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h   = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_cnot
  %c, %t = ftqc.logical_cnot %h, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  func.return %c, %t
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
