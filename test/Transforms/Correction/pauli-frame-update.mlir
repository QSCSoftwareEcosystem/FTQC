// RUN: ftqc-opt %s --ftqc-pauli-frame-opt | FileCheck %s
// Test: Deferred correction via Pauli frame update. (test 60)

// CHECK-LABEL: func @pauli_frame_deferred
func.func @pauli_frame_deferred() {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %lq2, %syn = ftqc.extract_syndrome %lq
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
  %lq3 = ftqc.correct %lq2, %syn
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.syndrome<6>)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  // After --ftqc-pauli-frame-opt, correct is replaced by frame update.
  // CHECK: ftqc.pauli_frame_update
  // CHECK-NOT: ftqc.correct
  // Note: %lq3 is now !ftqc.pauli_frame<1> after the pass, so no dealloc here.
  func.return
}
