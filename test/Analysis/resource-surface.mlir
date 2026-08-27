// RUN: ftqc-opt %s --ftqc-resource-estimate | FileCheck %s
// Test: Resource estimation for a surface-code circuit. (test 75)

// CHECK-LABEL: func @resource_estimate_surface
func.func @resource_estimate_surface() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %h   = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %c, %t = ftqc.logical_cnot %h, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)
  // CHECK: ftqc.resource_annotation
  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>) -> i1
  func.return
}
