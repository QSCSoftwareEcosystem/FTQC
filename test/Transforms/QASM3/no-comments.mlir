// RUN: ftqc-opt %s --ftqc-lower-to-qasm3="include-comments=false" 2>&1 | FileCheck %s
//
// Test: With include-comments=false, FT-infrastructure ops (syndrome,
// correction) produce no output at all — not even a comment line.
// The qubit name must still propagate correctly through the silent ops
// so that the final measure targets the right qubit.

// CHECK:     OPENQASM 3.0;
// CHECK:     qubit q0;
// CHECK:     h q0;
// CHECK-NOT: steane_x_syndrome
// CHECK-NOT: steane_z_syndrome
// CHECK-NOT: correct
// CHECK:     bit b0 = measure q0;

module {
  func.func @no_comments() -> i1 {
    %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %h0 = ftqc.logical_h %lq : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %out_x, %syn_x = ftqc.steane_x_syndrome %h0
        : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
    %out_z, %syn_z = ftqc.steane_z_syndrome %out_x
        : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
    %corrected = ftqc.correct %out_z, %syn_z
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
        -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %b = ftqc.logical_measure %corrected
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
    func.return %b : i1
  }
}
