// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: Surface-code circuit with CZ gates emits "cz" in QASM and uses
// the correct qubit names across the two-qubit op.

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK: qubit q1;
// CHECK: h q0;
// CHECK: h q1;
// CHECK: cz q0, q1;
// CHECK: bit b0 = measure q0;
// CHECK: bit b1 = measure q1;

module {
  func.func @grover_oracle() -> (i1, i1) {
    %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
    %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
    %h0  = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
    %h1  = ftqc.logical_h %lq1 : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
    %r0, %r1 = ftqc.logical_cz %h0, %h1
        : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>,
           !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>,
            !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>)
    %b0 = ftqc.logical_measure %r0
        : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>) -> i1
    %b1 = ftqc.logical_measure %r1
        : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>) -> i1
    func.return %b0, %b1 : i1, i1
  }
}
