// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate | FileCheck %s
//
// Benchmark B5 — QAOA MaxCut layer (4-node ring), FIXED-ANGLE instance.
//
// One QAOA layer on the ring 0-1-2-3-0:
//   cost   U_C(gamma) = prod_{(i,j) in E} exp(-i gamma Z_i Z_j)
//   mixer  U_M(beta)  = prod_i exp(-i beta X_i)
// The rotation angles are fixed to the Clifford+T-exact value pi/4, so each
//   Rzz(pi/4) = CNOT_{i,j} . (I (x) T)_j . CNOT_{i,j}      (cost, per edge)
//   Rx(pi/4)  = H_i . T_i . H_i                            (mixer, per qubit)
// is expressed exactly in the native Clifford+T set (T via magic injection).
// NOTE: ARBITRARY gamma/beta need the Rz(theta)->Clifford+T synthesis pass
// (roadmap item B1); this benchmark exercises the QAOA circuit STRUCTURE and
// its T-cost/distillation demand within the current two-level pipeline.
//
// CHECK-LABEL: func @qaoa_maxcut_ring4
// CHECK: ftqc.resource_annotation
// CHECK-SAME: tGateCount = 8

func.func @qaoa_maxcut_ring4() -> (i1, i1, i1, i1) {
  %q0_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- cost layer: Rzz(pi/4) on ring edges (0,1),(1,2),(2,3),(3,0) ---
  %q0_1, %q1_2 = ftqc.logical_cnot %q0_0, %q1_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %m3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q1_3 = ftqc.logical_t %q1_2, %m3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_4, %q1_5 = ftqc.logical_cnot %q0_1, %q1_3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q1_6, %q2_7 = ftqc.logical_cnot %q1_5, %q2_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %m8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q2_8 = ftqc.logical_t %q2_7, %m8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_9, %q2_10 = ftqc.logical_cnot %q1_6, %q2_8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q2_11, %q3_12 = ftqc.logical_cnot %q2_10, %q3_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %m13 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q3_13 = ftqc.logical_t %q3_12, %m13
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_14, %q3_15 = ftqc.logical_cnot %q2_11, %q3_13
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q3_16, %q0_17 = ftqc.logical_cnot %q3_15, %q0_4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %m18 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q0_18 = ftqc.logical_t %q0_17, %m18
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_19, %q0_20 = ftqc.logical_cnot %q3_16, %q0_18
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  // --- mixer layer: Rx(pi/4) on every qubit ---
  %q0_21 = ftqc.logical_h %q0_20 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m22 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q0_22 = ftqc.logical_t %q0_21, %m22
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q0_23 = ftqc.logical_h %q0_22 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_24 = ftqc.logical_h %q1_9 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m25 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q1_25 = ftqc.logical_t %q1_24, %m25
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_26 = ftqc.logical_h %q1_25 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_27 = ftqc.logical_h %q2_14 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m28 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q2_28 = ftqc.logical_t %q2_27, %m28
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_29 = ftqc.logical_h %q2_28 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_30 = ftqc.logical_h %q3_19 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m31 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q3_31 = ftqc.logical_t %q3_30, %m31
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_32 = ftqc.logical_h %q3_31 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- measure ---
  %b0 = ftqc.logical_measure %q0_23 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b1 = ftqc.logical_measure %q1_26 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b2 = ftqc.logical_measure %q2_29 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b3 = ftqc.logical_measure %q3_32 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b0, %b1, %b2, %b3 : i1, i1, i1, i1
}
