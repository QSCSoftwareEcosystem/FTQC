// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate \
// RUN:   | FileCheck %s
// Test: 3-qubit Quantum Fourier Transform encoded in Steane [[7,1,3]] code. (test 77)
//
// The 3-qubit QFT circuit uses:
//   - 3 Hadamard gates
//   - 3 controlled-phase gates (decomposed into T and S via magic-state injection
//     for non-Clifford Rz(π/4), Rz(π/8))
//   - 2 SWAP gates (decomposed as 3 CNOTs each)
//
// This tests the full Clifford+T fault-tolerant pipeline including
// magic-state distillation for T gates.

// CHECK-LABEL: func @qft3_steane
// CHECK:       ftqc.resource_annotation
// CHECK:       ftqc.logical_h
// CHECK:       ftqc.logical_t
// CHECK:       ftqc.logical_cnot

func.func @qft3_steane(
    %q0 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %q1 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
    %q2 : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>) {

  //--- QFT on q0 ----------------------------------------------------------
  // H on q0
  %h0 = ftqc.logical_h %q0
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // Controlled-S (Rz(π/2)) from q1 → q0: implemented as CNOT + T + CNOT + T†
  // Decomposition: CRz(π/2) = CNOT · (I⊗T†) · CNOT · (I⊗T) · (T†⊗I)
  // [DEBUG] Actual implementation: (I⊗T) · CNOT · (I⊗T) · CNOT
  %magic_t_0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0a = ftqc.logical_t %h0, %magic_t_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0a, %t1a = ftqc.logical_cnot %q1, %t0a
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %magic_t_1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0b = ftqc.logical_t %t1a, %magic_t_1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0b, %t1b = ftqc.logical_cnot %c0a, %t0b
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // Controlled-T (Rz(π/4)) from q2 → q0
  %magic_t_2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0c = ftqc.logical_t %t1b, %magic_t_2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0c, %t2a = ftqc.logical_cnot %q2, %t0c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %magic_t_3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0d = ftqc.logical_t %t2a, %magic_t_3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0d, %t2b = ftqc.logical_cnot %c0c, %t0d
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  //--- QFT on q1 ----------------------------------------------------------
  %h1 = ftqc.logical_h %c0b
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // Controlled-S from q2 → q1
  %magic_t_4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t1c = ftqc.logical_t %h1, %magic_t_4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c1a, %t2c = ftqc.logical_cnot %t2b, %t1c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %magic_t_5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t1d = ftqc.logical_t %t2c, %magic_t_5
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c1b, %t2d = ftqc.logical_cnot %c1a, %t1d
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  //--- QFT on q2 ----------------------------------------------------------
  %h2 = ftqc.logical_h %t2d
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  //--- SWAP q0 ↔ q2 (3 CNOTs) ---------------------------------------------
  %s0a, %s2a = ftqc.logical_cnot %c0d, %h2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %s2b, %s0b = ftqc.logical_cnot %s2a, %s0a
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %s0c, %s2c = ftqc.logical_cnot %s0b, %s2b
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  func.return %s0c, %c1b, %s2c
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
