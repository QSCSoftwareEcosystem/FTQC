// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: All single-qubit Clifford gates (x, y, z, h, s) produce the
// correct QASM gate names.

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK: x q0;
// CHECK: y q0;
// CHECK: z q0;
// CHECK: h q0;
// CHECK: s q0;
// CHECK: bit b0 = measure q0;

module {
  func.func @clifford_all() -> i1 {
    %lq  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %x0  = ftqc.logical_x %lq  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %y0  = ftqc.logical_y %x0  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %z0  = ftqc.logical_z %y0  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %h0  = ftqc.logical_h %z0  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %s0  = ftqc.logical_s %h0  : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %b   = ftqc.logical_measure %s0
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    func.return %b : i1
  }
}
