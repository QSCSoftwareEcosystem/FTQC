// RUN: ftqc-opt %s | FileCheck %s
// RUN: ftqc-opt %s --ftqc-resource-estimate 2>&1 | FileCheck %s --check-prefix=RES
//
// Benchmark A8 — MAGIC-STATE DISTILLATION FACTORY (standalone).
//
// The paper's headline resource number (a T gate costs ~195 physical
// distillation qubits, dominating a small circuit's own footprint) is here
// realized as a first-class compiled artifact rather than an analytic count.
// A 15-to-1 Bravyi-Kitaev factory consumes 15 noisy |T> states and emits one
// state of error O(eps^3); a two-level factory (15 level-1 outputs distilled
// again) reaches O(eps^9) and consumes 15 x 15 = 225 raw states -- exercising
// the Magic op category end-to-end and the distillation-qubit accounting in
// the resource estimator, within the current two-level pipeline.

// The estimator's module summary counts every distill op (1 + 16 = 17) and the
// per-T-gate distillation footprint (195 physical qubits for one Steane factory).
// RES: Distillation rounds   : 17
// RES: Distillation qubits   : 195

// CHECK-LABEL: func @factory_15to1
// A single 15-to-1 factory: 15 raw inputs, one distill op.
// CHECK-COUNT-15: ftqc.prepare_magic_t
// CHECK: ftqc.distill_15to1
func.func @factory_15to1() -> i1 {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %r0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r6 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r7 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r9 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r10 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r11 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r12 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r13 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r14 = ftqc.prepare_magic_t : !ftqc.magic_state
  %clean = ftqc.distill_15to1(%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8, %r9, %r10, %r11, %r12, %r13, %r14 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %o = ftqc.logical_t %lq, %clean : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b : i1
}

// CHECK-LABEL: func @factory_15to1_two_level
// Two-level factory: 225 raw inputs, 15 level-1 distills + 1 level-2 distill.
// CHECK-COUNT-16: ftqc.distill_15to1
func.func @factory_15to1_two_level() -> i1 {
  %lq = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %r0 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r1 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r2 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r3 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r4 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r5 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r6 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r7 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r8 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r9 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r10 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r11 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r12 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r13 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r14 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_0 = ftqc.distill_15to1(%r0, %r1, %r2, %r3, %r4, %r5, %r6, %r7, %r8, %r9, %r10, %r11, %r12, %r13, %r14 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r15 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r16 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r17 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r18 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r19 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r20 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r21 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r22 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r23 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r24 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r25 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r26 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r27 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r28 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r29 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_1 = ftqc.distill_15to1(%r15, %r16, %r17, %r18, %r19, %r20, %r21, %r22, %r23, %r24, %r25, %r26, %r27, %r28, %r29 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r30 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r31 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r32 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r33 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r34 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r35 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r36 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r37 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r38 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r39 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r40 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r41 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r42 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r43 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r44 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_2 = ftqc.distill_15to1(%r30, %r31, %r32, %r33, %r34, %r35, %r36, %r37, %r38, %r39, %r40, %r41, %r42, %r43, %r44 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r45 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r46 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r47 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r48 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r49 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r50 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r51 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r52 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r53 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r54 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r55 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r56 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r57 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r58 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r59 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_3 = ftqc.distill_15to1(%r45, %r46, %r47, %r48, %r49, %r50, %r51, %r52, %r53, %r54, %r55, %r56, %r57, %r58, %r59 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r60 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r61 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r62 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r63 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r64 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r65 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r66 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r67 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r68 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r69 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r70 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r71 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r72 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r73 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r74 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_4 = ftqc.distill_15to1(%r60, %r61, %r62, %r63, %r64, %r65, %r66, %r67, %r68, %r69, %r70, %r71, %r72, %r73, %r74 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r75 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r76 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r77 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r78 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r79 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r80 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r81 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r82 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r83 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r84 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r85 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r86 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r87 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r88 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r89 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_5 = ftqc.distill_15to1(%r75, %r76, %r77, %r78, %r79, %r80, %r81, %r82, %r83, %r84, %r85, %r86, %r87, %r88, %r89 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r90 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r91 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r92 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r93 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r94 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r95 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r96 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r97 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r98 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r99 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r100 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r101 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r102 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r103 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r104 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_6 = ftqc.distill_15to1(%r90, %r91, %r92, %r93, %r94, %r95, %r96, %r97, %r98, %r99, %r100, %r101, %r102, %r103, %r104 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r105 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r106 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r107 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r108 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r109 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r110 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r111 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r112 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r113 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r114 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r115 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r116 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r117 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r118 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r119 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_7 = ftqc.distill_15to1(%r105, %r106, %r107, %r108, %r109, %r110, %r111, %r112, %r113, %r114, %r115, %r116, %r117, %r118, %r119 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r120 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r121 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r122 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r123 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r124 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r125 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r126 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r127 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r128 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r129 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r130 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r131 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r132 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r133 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r134 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_8 = ftqc.distill_15to1(%r120, %r121, %r122, %r123, %r124, %r125, %r126, %r127, %r128, %r129, %r130, %r131, %r132, %r133, %r134 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r135 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r136 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r137 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r138 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r139 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r140 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r141 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r142 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r143 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r144 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r145 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r146 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r147 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r148 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r149 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_9 = ftqc.distill_15to1(%r135, %r136, %r137, %r138, %r139, %r140, %r141, %r142, %r143, %r144, %r145, %r146, %r147, %r148, %r149 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r150 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r151 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r152 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r153 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r154 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r155 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r156 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r157 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r158 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r159 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r160 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r161 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r162 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r163 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r164 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_10 = ftqc.distill_15to1(%r150, %r151, %r152, %r153, %r154, %r155, %r156, %r157, %r158, %r159, %r160, %r161, %r162, %r163, %r164 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r165 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r166 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r167 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r168 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r169 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r170 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r171 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r172 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r173 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r174 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r175 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r176 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r177 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r178 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r179 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_11 = ftqc.distill_15to1(%r165, %r166, %r167, %r168, %r169, %r170, %r171, %r172, %r173, %r174, %r175, %r176, %r177, %r178, %r179 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r180 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r181 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r182 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r183 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r184 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r185 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r186 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r187 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r188 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r189 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r190 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r191 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r192 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r193 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r194 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_12 = ftqc.distill_15to1(%r180, %r181, %r182, %r183, %r184, %r185, %r186, %r187, %r188, %r189, %r190, %r191, %r192, %r193, %r194 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r195 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r196 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r197 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r198 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r199 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r200 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r201 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r202 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r203 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r204 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r205 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r206 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r207 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r208 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r209 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_13 = ftqc.distill_15to1(%r195, %r196, %r197, %r198, %r199, %r200, %r201, %r202, %r203, %r204, %r205, %r206, %r207, %r208, %r209 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %r210 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r211 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r212 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r213 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r214 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r215 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r216 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r217 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r218 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r219 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r220 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r221 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r222 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r223 = ftqc.prepare_magic_t : !ftqc.magic_state
  %r224 = ftqc.prepare_magic_t : !ftqc.magic_state
  %d1_14 = ftqc.distill_15to1(%r210, %r211, %r212, %r213, %r214, %r215, %r216, %r217, %r218, %r219, %r220, %r221, %r222, %r223, %r224 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %top = ftqc.distill_15to1(%d1_0, %d1_1, %d1_2, %d1_3, %d1_4, %d1_5, %d1_6, %d1_7, %d1_8, %d1_9, %d1_10, %d1_11, %d1_12, %d1_13, %d1_14 : !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state, !ftqc.magic_state) -> !ftqc.magic_state
  %o = ftqc.logical_t %lq, %top : (!ftqc.logical_qubit<#ftqc.ecc<steane>>, !ftqc.magic_state) -> !ftqc.logical_qubit<#ftqc.ecc<steane>>
  %b = ftqc.logical_measure %o : (!ftqc.logical_qubit<#ftqc.ecc<steane>>) -> i1
  return %b : i1
}
