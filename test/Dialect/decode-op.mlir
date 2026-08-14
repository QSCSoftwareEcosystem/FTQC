// RUN: ftqc-opt %s | FileCheck %s
// Test: ftqc.decode operation.

// CHECK-LABEL: func @decode_steane
func.func @decode_steane(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1 {
  // CHECK: ftqc.decode %{{.*}} : <#ftqc.ecc<steane, 3>> -> i1
  %bit = ftqc.decode %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> -> i1
  func.return %bit : i1
}
