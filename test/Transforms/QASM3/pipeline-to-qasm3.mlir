// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-lower-to-qasm3 \
// RUN:   2>&1 | FileCheck %s
//
// Test: Bell-state run through the full FT pipeline (syndrome insertion +
// transversal gate decomposition) then lowered to QASM 3.0.
// After --ftqc-insert-syndrome the circuit gains steane_x_syndrome /
// steane_z_syndrome ops, which must appear as QASM comments.

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK: qubit q1;
// CHECK: h q0;
// CHECK: // steane_x_syndrome
// CHECK: // steane_z_syndrome
// CHECK: cx q0, q1;
// CHECK: bit b0 = measure q0;
// CHECK: bit b1 = measure q1;

module {
  func.func @bell_state_pipeline() -> (i1, i1) {
    %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %h0  = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %c, %t = ftqc.logical_cnot %h0, %lq1
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
           !ftqc.logical_qubit<#ftqc.ecc<steane>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
            !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    %b0 = ftqc.logical_measure %c
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    %b1 = ftqc.logical_measure %t
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    func.return %b0, %b1 : i1, i1
  }
}
