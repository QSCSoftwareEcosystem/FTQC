// RUN: ftqc-opt %s | FileCheck %s
// Test: ftqc.encode operation round-trip.

// CHECK-LABEL: func @encode_i1
func.func @encode_i1(%q: i1) -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>> {
  // CHECK: ftqc.encode %{{.*}} : i1 -> <#ftqc.ecc<steane, 3>>
  %lq = ftqc.encode %q : i1 -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}

// CHECK-LABEL: func @encode_surface
func.func @encode_surface(%q: i1) -> !ftqc.logical_qubit<#ftqc.ecc<surface, 5>> {
  // CHECK: ftqc.encode %{{.*}} : i1 -> <#ftqc.ecc<surface, 5>>
  %lq = ftqc.encode %q : i1 -> !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  func.return %lq : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
}
