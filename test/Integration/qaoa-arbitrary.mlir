// RUN: ftqc-opt %s --ftqc-synthesize-rz --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates --ftqc-resource-estimate 2>&1 | FileCheck %s
//
// Benchmark: QAOA MaxCut layer (4-node ring) at ARBITRARY angles
// gamma=0.4, beta=0.7 -- Rzz(2*gamma) = CNOT . Rz(0.8) . CNOT per edge, and
// Rx(2*beta) = H . Rz(1.4) . H per qubit.  The non-Clifford Rz rotations are
// approximated to Clifford+T by --ftqc-synthesize-rz (Solovay-Kitaev), which
// the earlier fixed-pi/4 QAOA benchmark could not do.
//
// CHECK-LABEL: func @qaoa_arb
// CHECK-NOT: ftqc.logical_rz
// CHECK: ftqc.resource_annotation

func.func @qaoa_arb() -> (i1, i1, i1, i1) {
  %q0_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- cost: Rzz(0.8) on ring edges ---
  %q0_1, %q1_2 = ftqc.logical_cnot %q0_0, %q1_0 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q1_3 = ftqc.logical_rz %q1_2, 0.8 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_4, %q1_5 = ftqc.logical_cnot %q0_1, %q1_3 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q1_6, %q2_7 = ftqc.logical_cnot %q1_5, %q2_0 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q2_8 = ftqc.logical_rz %q2_7, 0.8 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_9, %q2_10 = ftqc.logical_cnot %q1_6, %q2_8 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q2_11, %q3_12 = ftqc.logical_cnot %q2_10, %q3_0 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q3_13 = ftqc.logical_rz %q3_12, 0.8 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_14, %q3_15 = ftqc.logical_cnot %q2_11, %q3_13 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q3_16, %q0_17 = ftqc.logical_cnot %q3_15, %q0_4 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q0_18 = ftqc.logical_rz %q0_17, 0.8 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_19, %q0_20 = ftqc.logical_cnot %q3_16, %q0_18 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  // --- mixer: Rx(1.4) on each qubit ---
  %q0_21 = ftqc.logical_h %q0_20 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_22 = ftqc.logical_rz %q0_21, 1.4 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_23 = ftqc.logical_h %q0_22 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_24 = ftqc.logical_h %q1_9 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_25 = ftqc.logical_rz %q1_24, 1.4 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_26 = ftqc.logical_h %q1_25 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_27 = ftqc.logical_h %q2_14 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_28 = ftqc.logical_rz %q2_27, 1.4 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_29 = ftqc.logical_h %q2_28 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_30 = ftqc.logical_h %q3_19 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_31 = ftqc.logical_rz %q3_30, 1.4 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_32 = ftqc.logical_h %q3_31 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %b0 = ftqc.logical_measure %q0_23 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b1 = ftqc.logical_measure %q1_26 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b2 = ftqc.logical_measure %q2_29 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b3 = ftqc.logical_measure %q3_32 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b0, %b1, %b2, %b3 : i1, i1, i1, i1
}
