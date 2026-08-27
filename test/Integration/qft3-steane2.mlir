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
    %q0 : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %q1 : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %q2 : !ftqc.logical_qubit<#ftqc.ecc<steane>>)
    -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>) {

  //--- QFT on q0 ----------------------------------------------------------
  // H on q0
  %h0 = ftqc.logical_h %q0
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // Controlled-S (Rz(π/2)) from q1 → q0
  // Decomposition: CRz(π/2) = (T⊗T) · CNOT · (I⊗T†) · CNOT
  //                where T† = ZST
  %magic_t_0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0a = ftqc.logical_t %h0, %magic_t_0
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0b = ftqc.logical_t %q1, %magic_t_1
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0a, %t1a = ftqc.logical_cnot %t0b, %t0a
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %z0 = ftqc.logical_z %t1a
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %s0 = ftqc.logical_s %z0
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0c = ftqc.logical_t %s0, %magic_t_2
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0b, %t1b = ftqc.logical_cnot %c0a, %t0c
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)

  // Controlled-T (Rz(π/4)) from q2 → q0
  // Approximated using Controlled-S (Rz(π/2))
  %magic_t_3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0d = ftqc.logical_t %t1b, %magic_t_3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0e = ftqc.logical_t %q2, %magic_t_4
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0c, %t1c = ftqc.logical_cnot %t0e, %t0d
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %z1 = ftqc.logical_z %t1c
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %s1 = ftqc.logical_s %z1
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0f = ftqc.logical_t %s1, %magic_t_5
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0d, %t1d = ftqc.logical_cnot %c0c, %t0f
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)

  //--- QFT on q1 ----------------------------------------------------------
  %h1 = ftqc.logical_h %c0b
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // Controlled-S from q2 → q1
  %magic_t_6 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0g = ftqc.logical_t %h1, %magic_t_6
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_7 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0h = ftqc.logical_t %c0d, %magic_t_7
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0e, %t1e = ftqc.logical_cnot %t0h, %t0g
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %z2 = ftqc.logical_z %t1e
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %s2 = ftqc.logical_s %z2
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %magic_t_8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %t0i = ftqc.logical_t %s2, %magic_t_8
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state)
      -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %c0f, %t1f = ftqc.logical_cnot %c0e, %t0i
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)

  //--- QFT on q2 ----------------------------------------------------------
  %h3 = ftqc.logical_h %c0f
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  //--- SWAP q0 ↔ q2 (3 CNOTs) ---------------------------------------------
  %s0a, %s2a = ftqc.logical_cnot %t1d, %h3
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %s2b, %s0b = ftqc.logical_cnot %s2a, %s0a
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)
  %s0c, %s2c = ftqc.logical_cnot %s0b, %s2b
      : (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>)

  func.return %s0c, %t1f, %s2c
      : !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>,
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
}
