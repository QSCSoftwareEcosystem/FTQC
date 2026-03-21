// RUN: ftqc-opt %s \
// RUN:   "--ftqc-lower-to-stim=physical-error-rate=0 syndrome-rounds=1 include-comments=false" \
// RUN:   2>&1 | FileCheck %s
//
// Test: --ftqc-lower-to-stim generates a valid STIM circuit for Bell state.
// With p=0 (noiseless), the circuit omits all DEPOLARIZE/X_ERROR instructions.
// The emitter uses STIM abbreviated instruction names (R, M, H, CX).
//
// ── Logical qubit 0 encoding (Steane [[7,1,3]], qubits 0-6) ──────────────
// CHECK: R 0 1 2 3 4 5 6
// CHECK: H 0 1 3
// CHECK: CX 0 2
// CHECK: CX 0 4
// CHECK: CX 0 6
// CHECK: CX 1 2
// CHECK: CX 1 5
// CHECK: CX 1 6
// CHECK: CX 3 4
// CHECK: CX 3 5
// CHECK: CX 3 6
// ── Logical qubit 0 initial X-syndrome (ancillae 7,8,9) ──────────────────
// CHECK: R 7 8 9
// CHECK: H 7 8 9
// CHECK: CX 7 0 7 2 7 4 7 6
// CHECK: CX 8 1 8 2 8 5 8 6
// CHECK: CX 9 3 9 4 9 5 9 6
// CHECK: H 7 8 9
// CHECK: M 7 8 9
// ── Logical qubit 0 initial Z-syndrome (ancillae 10,11,12) ───────────────
// CHECK: R 10 11 12
// CHECK: CX 0 10 2 10 4 10 6 10
// CHECK: CX 1 11 2 11 5 11 6 11
// CHECK: CX 3 12 4 12 5 12 6 12
// CHECK: M 10 11 12
// ── Six DETECTORs for the first syndrome round (no prior reference) ───────
// CHECK: DETECTOR rec[-6]
// CHECK: DETECTOR rec[-5]
// CHECK: DETECTOR rec[-4]
// CHECK: DETECTOR rec[-3]
// CHECK: DETECTOR rec[-2]
// CHECK: DETECTOR rec[-1]
// ── Transversal CNOT between logical qubit 0 (ctrl) and qubit 1 (tgt) ────
// CHECK: CX 0 13 1 14 2 15 3 16 4 17 5 18 6 19
// ── Final measurements and per-qubit parity observables ──────────────────
// CHECK: M 0 1 2 3 4 5 6
// CHECK: OBSERVABLE_INCLUDE(0) rec[-7] rec[-6] rec[-5] rec[-4] rec[-3] rec[-2] rec[-1]
// CHECK: M 13 14 15 16 17 18 19
// CHECK: OBSERVABLE_INCLUDE(1) rec[-7] rec[-6] rec[-5] rec[-4] rec[-3] rec[-2] rec[-1]
// ── Module attribute confirms STIM string was attached ────────────────────
// CHECK: ftqc.stim

func.func @bell_state_ft() -> (i1, i1) {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  %h0 = ftqc.logical_h %lq0
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  %c, %t = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  %b0 = ftqc.logical_measure %c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1
  %b1 = ftqc.logical_measure %t
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) -> i1

  func.return %b0, %b1 : i1, i1
}
