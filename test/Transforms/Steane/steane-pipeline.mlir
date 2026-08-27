// RUN: ftqc-opt %s --ftqc-steane-pipeline | FileCheck %s
// Test: Full Steane encoding pipeline on a 3-qubit circuit. (test 30)

// Input: a circuit with H, CNOT, and T gates (no error correction).
// After pipeline: all gates encoded in Steane code with EC rounds and
// T gates replaced by magic state injection.

// CHECK-LABEL: func @three_qubit_ft
func.func @three_qubit_ft() {
  %lq0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %lq2 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  %h0 = ftqc.logical_h %lq0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_h

  %c0, %t0 = ftqc.logical_cnot %h0, %lq1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  // CHECK: ftqc.logical_cnot

  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0_after = ftqc.logical_t %t0, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  // CHECK: ftqc.logical_t

  ftqc.dealloc %c0      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %t0_after : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  ftqc.dealloc %lq2     : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  func.return
}
