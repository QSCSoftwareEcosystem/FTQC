// RUN: ftqc-opt %s | FileCheck %s
// Test: All logical gate operations.

// CHECK-LABEL: func @logical_x
func.func @logical_x(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.logical_x
  %out = ftqc.logical_x %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @logical_z
func.func @logical_z(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.logical_z
  %out = ftqc.logical_z %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @logical_h
func.func @logical_h(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  // CHECK: ftqc.logical_h
  %out = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return %out : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @logical_cnot
func.func @logical_cnot(
    %ctrl: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %tgt:  !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  // CHECK: ftqc.logical_cnot
  %c2, %t2 = ftqc.logical_cnot %ctrl, %tgt
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  func.return %c2, %t2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
                          !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// CHECK-LABEL: func @logical_measure
func.func @logical_measure(%lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1 {
  // CHECK: ftqc.logical_measure
  %bit = ftqc.logical_measure %lq : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  func.return %bit : i1
}
