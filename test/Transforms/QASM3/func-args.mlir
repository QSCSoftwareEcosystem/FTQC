// RUN: ftqc-opt %s --ftqc-lower-to-qasm3 2>&1 | FileCheck %s
//
// Test: Functions that receive logical qubits as arguments (rather than
// allocating with init_zero) emit "qubit arg_N;" declarations and use
// the arg_N name throughout the body.

// CHECK: OPENQASM 3.0;
// CHECK: qubit arg_0;
// CHECK: qubit arg_2;
// CHECK: h arg_0;
// CHECK: cx arg_0, arg_2;

module {
  func.func @circuit_with_args(
      %q0  : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
      %cls : i1,
      %q2  : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {
    %h0 = ftqc.logical_h %q0
        : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
    %c, %t = ftqc.logical_cnot %h0, %q2
        : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
           !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
        -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
            !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    func.return %c, %t
        : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  }
}
