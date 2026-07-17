// RUN: ftqc-opt %s --ftqc-lower-to-lightstim-json 2>&1 | FileCheck %s

// CHECK: "format": "ftqc-lightstim-experiment"
// CHECK: "type": "memory"
// CHECK: "id": "patch_0"
// CHECK: "code": "hypergraph_product"
// CHECK: "distance": 3
// CHECK: "initial_basis": "Z"
// CHECK: "measurement_basis": "Z"
// CHECK: "classical_codes":
// CHECK: "code1": {{\[\[1, 1, 0\], \[0, 1, 1\]\]}}
// CHECK: "code2": {{\[\[1, 1, 0\], \[0, 1, 1\]\]}}
// CHECK: "type": "syndrome_extraction"
// CHECK: "patch": "patch_0"
// CHECK: "rounds": 2

#repetition3 = #ftqc.classical_code<3, [1,1,0, 0,1,1]>
#surface3 = #ftqc.ecc<hypergraph_product, code1 = #repetition3, code2 = #repetition3>

func.func @hypergraph_product_memory() -> i1 {
  %lq0 = ftqc.init_zero : <#surface3>
  %lq1, %syn = ftqc.extract_syndrome %lq0 {rounds = 2 : i32} : <#surface3> -> (<#surface3>, <12>)
  %b0 = ftqc.logical_measure %lq1 : (<#surface3>) -> i1
  func.return %b0 : i1
}
