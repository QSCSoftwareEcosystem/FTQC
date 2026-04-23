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

  // Controlled-S (Rz(π/2)) from q1 → q0
  // Decomposition: CRz(π/2) = (T⊗T) · CNOT · (I⊗T†) · CNOT
  //                where T† = ZST
  %magic_t_0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0a = ftqc.logical_t %h0, %magic_t_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %magic_t_1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0b = ftqc.logical_t %q1, %magic_t_1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0a, %t1a = ftqc.logical_cnot %t0b, %t0a
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %z0 = ftqc.logical_z %t1a
  %s0 = ftqc.logical_s %z0
  %magic_t_2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0c = ftqc.logical_t %s0, %magic_t_2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0b, %t1b = ftqc.logical_cnot %c0a, %t0c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  // Controlled-T (Rz(π/4)) from q2 → q0
  // Approximated using Controlled-S (Rz(π/2))
  %magic_t_3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0d = ftqc.logical_t %t1b, %magic_t_3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %magic_t_4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0e = ftqc.logical_t %q2, %magic_t_4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0c, %t1c = ftqc.logical_cnot %t0e, %t0d
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %z1 = ftqc.logical_z %t1c
  %s1 = ftqc.logical_s %z1
  %magic_t_5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0f = ftqc.logical_t %s1, %magic_t_5
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0d, %t1d = ftqc.logical_cnot %c0c, %t0f
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  //--- QFT on q1 ----------------------------------------------------------
  %h1 = ftqc.logical_h %c0b
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  // Controlled-S from q2 → q1
  %magic_t_6 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0g = ftqc.logical_t %h1, %magic_t_6
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %magic_t_7 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0h = ftqc.logical_t %c0d, %magic_t_7
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0e, %t1e = ftqc.logical_cnot %t0h, %t0g
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
  %z2 = ftqc.logical_z %t1e
  %s2 = ftqc.logical_s %z2
  %magic_t_8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0i = ftqc.logical_t %s2, %magic_t_8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
  %c0f, %t1f = ftqc.logical_cnot %c0e, %t0i
      : (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>)

  //--- QFT on q2 ----------------------------------------------------------
  %h3 = ftqc.logical_h %c0f
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>

  //--- SWAP q0 ↔ q2 (3 CNOTs) ---------------------------------------------
  %s0a, %s2a = ftqc.logical_cnot %t1d, %h3
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

  func.return %s0c, %t1f, %s2c
      : !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane, 3>>
}
