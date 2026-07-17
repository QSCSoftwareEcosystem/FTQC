// RUN: ftqc-opt %s --ftqc-lower-to-lightstim-json 2>&1 | FileCheck %s
//
// Usage with defaults:
//   ftqc-opt input.mlir --ftqc-lower-to-lightstim-json
//
// Usage with explicit experiment/simulation options:
//   ftqc-opt input.mlir --ftqc-lower-to-lightstim-json="physical-error-rate=0.001 surgery-rounds=5 patch-spacing=10 max-errors=100 max-shots=5000000"

// CHECK: "format": "ftqc-lightstim-experiment"
// CHECK: "type": "two_patch_lattice_surgery"
// CHECK: "id": "patch_0"
// CHECK: "code": "unrotated_surface_code"
// CHECK: "distance": 3
// CHECK: "initial_basis": "X"
// CHECK: "measurement_basis": "Z"
// CHECK: "id": "patch_1"
// CHECK: "distance": 5
// CHECK: "offset": [0, 10]
// CHECK: "initial_basis": "Z"
// CHECK: "interaction": "ZZ"
// CHECK: "patches": ["patch_0", "patch_1"]
// CHECK: "rounds": 5
// CHECK: "p_1q": {{.*}}
// CHECK: "name": "pymatching"
// CHECK: "max_errors": 100

func.func @lightstim_zz() {
  %p0 = ftqc.init_plus : !ftqc.logical_qubit<#ftqc.ecc<surface, 3>>
  %p1 = ftqc.init_zero : !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>
  %r0, %r1, %syn = ftqc.lattice_surgery %p0, %p1 kind = 1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
         !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>)
      -> (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>,
          !ftqc.logical_qubit<#ftqc.ecc<surface, 5>>,
          !ftqc.syndrome<16>)
  %m0 = ftqc.logical_measure %r0
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 3>>) -> i1
  %m1 = ftqc.logical_measure %r1
      : (!ftqc.logical_qubit<#ftqc.ecc<surface, 5>>) -> i1
  func.return
}
