// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: T gate via magic-state injection emits "t qN;" in QASM.
// prepare_magic_t must NOT emit a qubit declaration (it is not a qubit).

// CHECK: OPENQASM 3.0;
// CHECK: qubit q0;
// CHECK-NOT: qubit __magic_t
// CHECK: t q0;
// CHECK: bit b0 = measure q0;

module {
  func.func @t_gate() -> i1 {
    %lq  = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %ms  = ftqc.prepare_magic_t : !ftqc.magic_state
    %t0  = ftqc.logical_t %lq, %ms
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
        -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
    %b   = ftqc.logical_measure %t0
        : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
    func.return %b : i1
  }
}
