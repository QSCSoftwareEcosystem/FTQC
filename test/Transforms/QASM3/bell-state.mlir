// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: Bell-state circuit (init_zero + logical_h + logical_cnot + measure)
// produces well-formed QASM 3.0 with h and cx gates.

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK: qubit q1;
// CHECK: h q0;
// CHECK: cx q0, q1;
// CHECK: bit b0 = measure q0;
// CHECK: bit b1 = measure q1;

module {
  func.func @bell_state() -> (i1, i1) {
    %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %h0  = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %c, %t = ftqc.logical_cnot %h0, %lq1
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
           !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
            !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    %b0 = ftqc.logical_measure %c
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
    %b1 = ftqc.logical_measure %t
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
    func.return %b0, %b1 : i1, i1
  }
}
