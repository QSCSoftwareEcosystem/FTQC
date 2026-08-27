// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: Syndrome extraction ops produce QASM comments (not gates), and the
// qubit SSA name is correctly propagated through the syndrome op chain so
// that the final measurement references the right qubit.

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK: h q0;
// CHECK: // steane_x_syndrome
// CHECK: // steane_z_syndrome
// CHECK: bit b0 = measure q0;

module {
  func.func @syndrome_chain() -> i1 {
    %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %h0 = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %out_x, %syn_x = ftqc.steane_x_syndrome %h0
        : !ftqc.logical_qubit<#ftqc.ecc<steane>>
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
    %out_z, %syn_z = ftqc.steane_z_syndrome %out_x
        : !ftqc.logical_qubit<#ftqc.ecc<steane>>
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)
    %b = ftqc.logical_measure %out_z
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    func.return %b : i1
  }
}
