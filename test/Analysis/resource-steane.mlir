// RUN: ftqc-opt %s --ftqc-resource-estimate | FileCheck %s
// Test: Resource estimation for a Steane-code circuit. (test 74)

// CHECK-LABEL: func @resource_estimate_steane
func.func @resource_estimate_steane() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %h   = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c, %t = ftqc.logical_cnot %h, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  %t2 = ftqc.logical_t %t, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // After --ftqc-resource-estimate:
  // CHECK: ftqc.resource_annotation
  ftqc.dealloc %c  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %t2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
