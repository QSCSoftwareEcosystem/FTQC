// RUN: ftqc-opt -split-input-file %s --ftqc-synthesize-rz | FileCheck %s
//
// --ftqc-synthesize-rz replaces logical_rz by a Clifford+T sequence
// (Solovay-Kitaev).  Dyadic multiples of pi/4 are emitted exactly; arbitrary
// angles become an approximating H/S/T/Z/... sequence carrying a T-count.

// Exact case: Rz(pi/4) == T (one T gate, no residual rz).
// CHECK-LABEL: func @exact_t
// CHECK-NOT: ftqc.logical_rz
// CHECK: ftqc.logical_t
func.func @exact_t(%q: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %o = ftqc.logical_rz %q, 0.7853981633974483
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  return %o : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}

// -----

// Arbitrary angle: approximated by a Clifford+T word (no residual rz).
// CHECK-LABEL: func @approx
// CHECK-NOT: ftqc.logical_rz
// CHECK: ftqc.logical_
func.func @approx(%q: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> !ftqc.logical_qubit<#ftqc.ecc<steane>> {
  %o = ftqc.logical_rz %q, 0.3 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  return %o : !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
