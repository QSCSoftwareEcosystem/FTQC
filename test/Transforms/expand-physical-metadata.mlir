// RUN: ftqc-opt %s --ftqc-expand-physical 2>&1 | FileCheck %s
//
// --ftqc-expand-physical stamps QEC-structure metadata on each qphys.measure so
// a physical back end can reconstruct detectors/observables.  Two logical qubits,
// each with one syndrome round: ancilla measures carry role/stabilizer/round/
// logical; the stabilizer id is offset by 6 per logical qubit (X:0-2/6-8,
// Z:3-5/9-11), and data measures are tagged role="data" with their logical id.

func.func @two() -> (i1, i1) {
  %q0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %q1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %o0, %s0 = ftqc.ec_round %q0 rounds = 1 : <<steane, 3>> -> (<<steane, 3>>, <6>)
  %o1, %s1 = ftqc.ec_round %q1 rounds = 1 : <<steane, 3>> -> (<<steane, 3>>, <6>)
  %b0 = ftqc.logical_measure %o0 : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b1 = ftqc.logical_measure %o1 : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  return %b0, %b1 : i1, i1
}

// Logical 0: X-stabilizers 0-2, Z-stabilizers 3-5.
// CHECK-DAG: logical = 0 : i64, role = "x_anc", round = 0 : i64, stabilizer = 0
// CHECK-DAG: logical = 0 : i64, role = "z_anc", round = 0 : i64, stabilizer = 3
// Logical 1: X-stabilizers 6-8, Z-stabilizers 9-11 (offset by 6).
// CHECK-DAG: logical = 1 : i64, role = "x_anc", round = 0 : i64, stabilizer = 6
// CHECK-DAG: logical = 1 : i64, role = "z_anc", round = 0 : i64, stabilizer = 9
// Data measurements carry role="data" and their logical index.
// CHECK-DAG: key = "m", logical = 0 : i64, role = "data"
// CHECK-DAG: key = "m", logical = 1 : i64, role = "data"
