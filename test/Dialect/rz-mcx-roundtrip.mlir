// RUN: ftqc-opt %s | ftqc-opt | FileCheck %s
// Round-trip the logical_rz (Z-rotation) and logical_mcx (multi-controlled X) ops.

// CHECK-LABEL: func @rz_mcx
func.func @rz_mcx(
    %q: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %c0: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %c1: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %c2: !ftqc.logical_qubit<#ftqc.ecc<steane>>,
    %t: !ftqc.logical_qubit<#ftqc.ecc<steane>>) {
  // CHECK: ftqc.logical_rz %{{.*}}, 3.000000e-01 : <<steane>>
  %r = ftqc.logical_rz %q, 0.3 : !ftqc.logical_qubit<#ftqc.ecc<steane>>

  // CHECK: ftqc.logical_mcx[%{{.*}}, %{{.*}}, %{{.*}}], %{{.*}}
  %o:4 = ftqc.logical_mcx [%c0, %c1, %c2], %t
      : [!ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>,
         !ftqc.logical_qubit<#ftqc.ecc<steane>>],
        !ftqc.logical_qubit<#ftqc.ecc<steane>>
      -> [!ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>,
          !ftqc.logical_qubit<#ftqc.ecc<steane>>],
         !ftqc.logical_qubit<#ftqc.ecc<steane>>
  return
}
