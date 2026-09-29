#rep1 = #ftqc.classical_code<1, []>
#rep3 = #ftqc.classical_code<3, [1,1,0, 0,1,1]>
#surface3 = #ftqc.ecc<hypergraph_product, code1 = #rep3, code2 = #rep3>

#ring3 = #ftqc.classical_code<3, [1,1,0, 0,1,1, 1,0,1]>
#toric3 = #ftqc.ecc<hypergraph_product, code1 = #ring3, code2 = #ring3>

#parity1 = #ftqc.classical_code<2, [1,1]>
#parity2 = #ftqc.classical_code<3, [1,1,1]>
#parity3 = #ftqc.classical_code<4, [1,1,1,1]>
#detect2 = #ftqc.ecc<hypergraph_product, code1 = #parity2, code2 = #parity1>
#detect3 = #ftqc.ecc<hypergraph_product, code1 = #parity3, code2 = #parity1>
#detect4 = #ftqc.ecc<hypergraph_product, code1 = #parity2, code2 = #parity2>
#detect6 = #ftqc.ecc<hypergraph_product, code1 = #parity3, code2 = #parity2>

#rsurface3 = #ftqc.ecc<surface, dist = 3>
#steane = #ftqc.ecc<steane>

#bit_flip = #ftqc.ecc<hypergraph_product, code1 = #rep1, code2 = #rep3>
#phase_flip = #ftqc.ecc<hypergraph_product, code1 = #rep3, code2 = #rep1>
#shor = #ftqc.ecc<concat, outer = #phase_flip, inner = #bit_flip>

#code = #detect2

func.func @memory() -> () {
  %l0 = ftqc.init_zero : <#code>
  %l1, %syn = ftqc.extract_syndrome %l0 : <#code> -> (<#code>, <1>)
  %b0 = ftqc.logical_measure %l1 : (<#code>) -> i1
  return
}
