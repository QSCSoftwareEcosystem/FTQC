// RUN: ftqc-opt %s \
// RUN:   --ftqc-insert-syndrome \
// RUN:   --ftqc-transversal-gates \
// RUN:   --ftqc-resource-estimate \
// RUN:   | FileCheck %s
// Test: 2-qubit Grover search encoded in surface code distance-5. (test 78)
//
// Grover's algorithm on 2 qubits searching for |11>:
//   Phase 1 - Initialization:  H⊗H |00>
//   Phase 2 - Oracle:          Toffoli (marks |11> with phase flip)
//             Since 2-qubit Grover only needs 1 iteration:
//             Oracle: CZ (controlled-Z flips |11>)
//   Phase 3 - Diffusion:       H⊗H · (2|00><00| - I) · H⊗H
//             Diffusion: X⊗X · CZ · X⊗X (for 2-qubit case)
//   Measurement should yield |11> with probability 1 after 1 iteration.
//
// Uses surface code distance-5 for higher fault tolerance (p_L ~ 10^-7 at p=10^-3).

// CHECK-LABEL: func @grover_ft
// CHECK:       ftqc.resource_annotation
// CHECK:       ftqc.init_zero
// CHECK:       ftqc.logical_h
// CHECK:       ftqc.logical_cz
// CHECK:       ftqc.logical_x

func.func @grover_ft() -> (i1, i1) {
  // Initialize |00> in surface code distance-5
  %q0 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %q1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  //--- Phase 1: Equal superposition H⊗H |00> → (|00>+|01>+|10>+|11>)/2 ---

  %h0 = ftqc.logical_h %q0
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %h1 = ftqc.logical_h %q1
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  //--- Phase 2: Oracle - CZ marks |11> with a phase flip -------------------
  // CZ|11> = -|11>, others unchanged
  %o0, %o1 = ftqc.logical_cz %h0, %h1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)

  //--- Phase 3: Grover diffusion operator ----------------------------------
  // D = H⊗H · (2|00><00| - I) · H⊗H
  // 2|00><00| - I is implemented as: X⊗X · CZ · X⊗X (up to global phase)

  // H⊗H
  %d0a = ftqc.logical_h %o0
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %d1a = ftqc.logical_h %o1
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  // X⊗X  (flips |00> ↔ |11>, |01> ↔ |10>)
  %d0b = ftqc.logical_x %d0a
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %d1b = ftqc.logical_x %d1a
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  // CZ (conditional phase on |11> = the mapped |00>)
  %d0c, %d1c = ftqc.logical_cz %d0b, %d1b
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>)

  // X⊗X (undo the flip)
  %d0d = ftqc.logical_x %d0c
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %d1d = ftqc.logical_x %d1c
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  // H⊗H (complete diffusion)
  %d0e = ftqc.logical_h %d0d
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>
  %d1e = ftqc.logical_h %d1d
      : !ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>

  //--- Measurement: should yield |11> with probability 1 ------------------
  %b0 = ftqc.logical_measure %d0e
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>) -> i1
  %b1 = ftqc.logical_measure %d1e
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, dist = 5>>) -> i1

  func.return %b0, %b1 : i1, i1
}
