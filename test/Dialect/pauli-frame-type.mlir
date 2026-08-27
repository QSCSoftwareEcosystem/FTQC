// RUN: ftqc-opt %s | FileCheck %s
// Test: PauliFrameType round-trip.

// CHECK-LABEL: func @pauli_frame_basic
func.func @pauli_frame_basic() {
  // CHECK: ftqc.create_frame : <3>
  %frame = ftqc.create_frame : !ftqc.pauli_frame<3>

  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  %lq0x, %syn0 = ftqc.extract_syndrome %lq0
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.syndrome<6>)

  // CHECK: ftqc.pauli_frame_update
  %frame2 = ftqc.pauli_frame_update %frame, %syn0, 0
      : !ftqc.pauli_frame<3>, !ftqc.syndrome<6> -> !ftqc.pauli_frame<3>

  ftqc.dealloc %lq0x : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %lq1 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
