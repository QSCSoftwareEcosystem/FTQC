// RUN: ftqc-opt %s | FileCheck %s
// Test: Gate teleportation primitive. (test 45)

// CHECK-LABEL: func @teleport_t_gate
func.func @teleport_t_gate(
    %lq: !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, i1) {
  %ms = ftqc.prepare_magic_t : !ftqc.magic_state
  // CHECK: ftqc.gate_teleport
  %out, %mbit = ftqc.gate_teleport %lq, %ms
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>, i1)
  func.return %out, %mbit
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>, i1
}
