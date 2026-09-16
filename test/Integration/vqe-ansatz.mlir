// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate | FileCheck %s
//
// Benchmark B6 — VQE hardware-efficient ansatz block (4 qubits), FIXED-ANGLE.
//
// One ansatz layer: a single-qubit rotation sublayer, a linear CNOT
// entangling ladder, and a second rotation sublayer -- the canonical
// hardware-efficient VQE ansatz (a UCCSD single excitation has the same
// shape: basis change, CNOT ladder, central Rz, ladder back).
// Each variational single-qubit rotation is fixed to a Clifford+T-exact
// angle: Rz(3pi/4) = S . T  (logical_s then logical_t via magic injection).
// NOTE: ARBITRARY variational angles need the Rz(theta)->Clifford+T synthesis
// pass (roadmap item B1); this benchmark exercises the ansatz STRUCTURE and
// its T-cost within the current two-level pipeline (Level-1 logical IR).
//
// CHECK-LABEL: func @vqe_ansatz_hwe4
// CHECK: ftqc.resource_annotation
// 8 native logical_t gates; the 8 logical_s are transversal on the Steane code
// (self-dual CSS) and are NOT expanded to T*T, so the T-count is 8.
// CHECK-SAME: tGateCount = 8

func.func @vqe_ansatz_hwe4(
    %q0_0: !ftqc.logical_qubit<#ftqc.ecc<steane>>, %q1_0: !ftqc.logical_qubit<#ftqc.ecc<steane>>, %q2_0: !ftqc.logical_qubit<#ftqc.ecc<steane>>, %q3_0: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (i1, i1, i1, i1) {
  // --- rotation sublayer 1: Rz(3pi/4) = S.T on each qubit ---
  %q0_1 = ftqc.logical_s %q0_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q0_2 = ftqc.logical_t %q0_1, %m2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_3 = ftqc.logical_s %q1_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q1_4 = ftqc.logical_t %q1_3, %m4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_5 = ftqc.logical_s %q2_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m6 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q2_6 = ftqc.logical_t %q2_5, %m6
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_7 = ftqc.logical_s %q3_0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q3_8 = ftqc.logical_t %q3_7, %m8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- entangling ladder: linear CNOT chain 0-1-2-3 ---
  %q0_9, %q1_10 = ftqc.logical_cnot %q0_2, %q1_4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q1_11, %q2_12 = ftqc.logical_cnot %q1_10, %q2_6
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %q2_13, %q3_14 = ftqc.logical_cnot %q2_12, %q3_8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>) -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  // --- rotation sublayer 2 ---
  %q0_15 = ftqc.logical_s %q0_9 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m16 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q0_16 = ftqc.logical_t %q0_15, %m16
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q1_17 = ftqc.logical_s %q1_11 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m18 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q1_18 = ftqc.logical_t %q1_17, %m18
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q2_19 = ftqc.logical_s %q2_13 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m20 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q2_20 = ftqc.logical_t %q2_19, %m20
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %q3_21 = ftqc.logical_s %q3_14 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %m22 = ftqc.prepare_magic_t : !ftqc.magic_state
  %q3_22 = ftqc.logical_t %q3_21, %m22
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // --- measure ---
  %b0 = ftqc.logical_measure %q0_16 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b1 = ftqc.logical_measure %q1_18 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b2 = ftqc.logical_measure %q2_20 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  %b3 = ftqc.logical_measure %q3_22 : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b0, %b1, %b2, %b3 : i1, i1, i1, i1
}
