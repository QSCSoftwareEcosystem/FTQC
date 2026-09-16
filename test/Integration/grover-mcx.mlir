// RUN: ftqc-opt %s --ftqc-decompose-mcx | FileCheck %s
// RUN: ftqc-opt %s --ftqc-decompose-mcx --ftqc-resource-estimate 2>&1 | FileCheck %s --check-prefix=RES
//
// Benchmark: generalized GROVER (4 qubits, 1 iteration) with a REAL multi-
// controlled oracle.  The marked-state phase flip and the diffusion operator
// are multi-controlled-Z gates (MCZ = H . MCX . H), expressed with
// ftqc.logical_mcx and lowered by --ftqc-decompose-mcx into a Toffoli network
// (3-control MCX -> V-chain: 3 Toffoli + 1 ancilla).  This is the oracle
// structure the fixed 2-qubit Grover benchmark could not express.
//
// CHECK-LABEL: func @grover4
// CHECK-NOT: ftqc.logical_mcx
// CHECK-COUNT-6: ftqc.logical_toffoli
// RES: ftqc.resource_annotation

func.func @grover4() -> (i1, i1, i1, i1) {
  %q0_0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- superposition ---
  %q0_1 = ftqc.logical_h %q0_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_2 = ftqc.logical_h %q1_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_3 = ftqc.logical_h %q2_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_4 = ftqc.logical_h %q3_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- oracle: phase-flip |1111> (MCZ on all 4) ---
  %q3_5 = ftqc.logical_h %q3_4 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m6_0, %m6_1, %m6_2, %m6_3 = ftqc.logical_mcx [%q0_1, %q1_2, %q2_3], %q3_5 : [!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>], !ftqc.logical_qubit<#ftqc.ecc<steane>> -> [!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>], !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_7 = ftqc.logical_h %m6_3 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- diffusion ---
  %q0_8 = ftqc.logical_h %m6_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_9 = ftqc.logical_h %m6_1 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_10 = ftqc.logical_h %m6_2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_11 = ftqc.logical_h %q3_7 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_12 = ftqc.logical_x %q0_8 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_13 = ftqc.logical_x %q1_9 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_14 = ftqc.logical_x %q2_10 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_15 = ftqc.logical_x %q3_11 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_16 = ftqc.logical_h %q3_15 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m17_0, %m17_1, %m17_2, %m17_3 = ftqc.logical_mcx [%q0_12, %q1_13, %q2_14], %q3_16 : [!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>], !ftqc.logical_qubit<#ftqc.ecc<steane>> -> [!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>], !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_18 = ftqc.logical_h %m17_3 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_19 = ftqc.logical_x %m17_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_20 = ftqc.logical_x %m17_1 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_21 = ftqc.logical_x %m17_2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_22 = ftqc.logical_x %q3_18 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_23 = ftqc.logical_h %q0_19 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_24 = ftqc.logical_h %q1_20 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_25 = ftqc.logical_h %q2_21 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_26 = ftqc.logical_h %q3_22 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- measure ---
  %b0 = ftqc.logical_measure %q0_23 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b1 = ftqc.logical_measure %q1_24 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b2 = ftqc.logical_measure %q2_25 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b3 = ftqc.logical_measure %q3_26 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b0, %b1, %b2, %b3 : i1, i1, i1, i1
}
