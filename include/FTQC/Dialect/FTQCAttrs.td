//===- FTQCAttrs.td - FTQC Attribute Definitions --------------------------===//
//
// Attributes encoding quantum error-correcting code (QECC) parameters.
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_ATTRS_TD
#define FTQC_ATTRS_TD

include "FTQC/Dialect/FTQCDialect.td"
include "mlir/IR/BuiltinAttributeInterfaces.td"
include "mlir/IR/EnumAttr.td"

//===----------------------------------------------------------------------===//
// Enum Attributes
//===----------------------------------------------------------------------===//

// Error correcting code kind
def ECC_Steane            : I32EnumAttrCase<"Steane",            0, "steane">;
def ECC_Surface           : I32EnumAttrCase<"Surface",           1, "surface">;
def ECC_ColorCode         : I32EnumAttrCase<"ColorCode",         2, "color_code">;
def ECC_Concat            : I32EnumAttrCase<"Concatenated",      3, "concat">;
def ECC_Repetition        : I32EnumAttrCase<"Repetition",        4, "repetition">;
def ECC_Compass           : I32EnumAttrCase<"Compass",           5, "compass">;
def ECC_HypergraphProduct : I32EnumAttrCase<"HypergraphProduct", 6, "hypergraph_product">;

def ECCKindAttr : I32EnumAttr<
    "ECCKind", "Error correcting code kind",
    [ECC_Steane, ECC_Surface, ECC_ColorCode, ECC_Concat,
     ECC_Repetition, ECC_Compass, ECC_HypergraphProduct]> {
  let cppNamespace = "::mlir::ftqc";
  let genSpecializedAttr = 0;
}

// Pauli operator type
def Pauli_I : I32EnumAttrCase<"I", 0, "I">;
def Pauli_X : I32EnumAttrCase<"X", 1, "X">;
def Pauli_Y : I32EnumAttrCase<"Y", 2, "Y">;
def Pauli_Z : I32EnumAttrCase<"Z", 3, "Z">;

def PauliKindAttr : I32EnumAttr<
    "PauliKind", "Single-qubit Pauli operator",
    [Pauli_I, Pauli_X, Pauli_Y, Pauli_Z]> {
  let cppNamespace = "::mlir::ftqc";
  let genSpecializedAttr = 0;
}

// Syndrome basis
def Basis_X : I32EnumAttrCase<"X", 0, "X">;
def Basis_Z : I32EnumAttrCase<"Z", 1, "Z">;

def SyndromeBasisAttr : I32EnumAttr<
    "SyndromeBasis", "Syndrome measurement basis",
    [Basis_X, Basis_Z]> {
  let cppNamespace = "::mlir::ftqc";
  let genSpecializedAttr = 0;
}

// Magic state kind
def Magic_T   : I32EnumAttrCase<"T",   0, "T">;
def Magic_CCZ : I32EnumAttrCase<"CCZ", 1, "CCZ">;
def Magic_CS  : I32EnumAttrCase<"CS",  2, "CS">;

def MagicKindAttr : I32EnumAttr<
    "MagicKind", "Magic state type",
    [Magic_T, Magic_CCZ, Magic_CS]> {
  let cppNamespace = "::mlir::ftqc";
  let genSpecializedAttr = 0;
}

//===----------------------------------------------------------------------===//
// Struct-style Attribute Definitions (ODS AttrDef)
//===----------------------------------------------------------------------===//

class FTQC_Attr<string name, string mnem, list<Trait> traits = []>
    : AttrDef<FTQC_Dialect, name, traits> {
  let mnemonic = mnem;
}

//----------------------------------------------------------------------
// ECCAttr — encodes code kind + kind-dependent parameters
//----------------------------------------------------------------------
def FTQC_ECCAttr : FTQC_Attr<"ECC", "ecc"> {
  let summary = "Error correcting code descriptor";
  let description = [{
    Describes an error correcting code with its kind and parameters.

    Examples:
    ```
    #ftqc.ecc<steane>                                       // Steane [[7,1,3]]
    #ftqc.ecc<surface, dist = 3>                            // Distance-3 rotated surface code
    #ftqc.ecc<color_code, dist = 5>                         // Distance-5 color code
    #ftqc.ecc<concat, outer = <steane>, inner = <steane>>   // 2-level concatenated Steane
    #cc = #ftqc.classical_code<3, [
      1, 1, 0,
      0, 1, 1,
      1, 0, 1
    ]>
    #ftqc.ecc<hypergraph_product, code1 = #cc, code2 = #cc> // Distance-3 toric code
    ```
  }];

  let parameters = (ins
    "ECCKind" : $kind,
    OptionalParameter<"std::optional<uint32_t>"> : $dist,
    OptionalParameter<"ECCAttr"> : $outer,
    OptionalParameter<"ECCAttr"> : $inner,
    OptionalParameter<"ClassicalCodeAttr"> : $code1,
    OptionalParameter<"ClassicalCodeAttr"> : $code2
  );

  let builders = [
    AttrBuilder<(ins "ECCKind":$kind, "std::optional<uint32_t>":$dist), [{
      return Base::get($_ctxt, kind, dist, nullptr, nullptr, nullptr, nullptr);
    }]>,
  ];

  let assemblyFormat = "`<` $kind (`,` struct($dist, $outer, $inner, $code1, $code2)^)? `>`";
  let genVerifyDecl = 1;
  let genStorageClass = 0;

  let extraClassDeclaration = [{
    /// Number of physical qubits per logical qubit.
    unsigned getPhysicalQubits() const;
    /// Number of ancilla qubits for one syndrome-extraction round.
    unsigned getAncillaQubits() const;
    /// Code distance. This is defined for all kinds, while getDist() returns an
    /// optional parameter only required for some kinds.
    unsigned getDistance() const;
    /// Fault-tolerance threshold (approximate, code-dependent).
    double getFTThreshold() const;
    /// StabilizerCode object describing code parameters, stabilizers, and
    /// logical X/Z operators.
    const ::mlir::ftqc::StabilizerCode &getStabilizerCode() const;
  }];
}

//----------------------------------------------------------------------
// ClassicalCode — a classical linear binary code
//----------------------------------------------------------------------
def FTQC_ClassicalCodeAttr : FTQC_Attr<"ClassicalCode", "classical_code"> {
  let summary = "An [n,k,d] classical linear binary code";
  let parameters = (ins
    "uint32_t" : $n,
    "DenseI8ArrayAttr" : $H
  );
  let assemblyFormat = "`<` $n `,` $H `>`";
  let extraClassDeclaration = [{
    ClassicalCode makeClassicalCode() const;
  }];
}

//----------------------------------------------------------------------
// PauliErrorAttr — a weight-n Pauli error on a logical block
//----------------------------------------------------------------------
def FTQC_PauliErrorAttr : FTQC_Attr<"PauliError", "pauli_error"> {
  let summary = "A Pauli error specification on a set of qubits";
  let description = [{
    Represents a tensor product of Pauli operators: e.g.,
    `#ftqc.pauli_error<[X, I, Y, Z]>` is X⊗I⊗Y⊗Z acting on qubits 0..3.
  }];
  let parameters = (ins "ArrayAttr":$paulis);
  let assemblyFormat = "`<` $paulis `>`";

  let extraClassDeclaration = [{
    unsigned getWeight() const;
    bool commutesWith(PauliErrorAttr other) const;
  }];
}

//----------------------------------------------------------------------
// CodePropertiesAttr — static [[n,k,d]] parameters for analysis passes
//----------------------------------------------------------------------
def FTQC_CodePropertiesAttr : FTQC_Attr<"CodeProperties", "code_props"> {
  let summary = "Static [[n,k,d]] code properties for annotation and analysis";
  let description = [{
    Annotates an operation with the static parameters of the error-correcting
    code in use:  [[n, k, d]]  where n = physical, k = logical, d = distance.
  }];
  let parameters = (ins
    "uint32_t" : $n,
    "uint32_t" : $k,
    "uint32_t" : $d
  );
  // threshold is derived via getFTThreshold(kind) at runtime
  let assemblyFormat = "`<` `n` `=` $n `,` `k` `=` $k `,` `d` `=` $d `>`";
}

//----------------------------------------------------------------------
// DistillationParamsAttr — parameters for a distillation protocol
//----------------------------------------------------------------------
def FTQC_DistillationParamsAttr : FTQC_Attr<"DistillationParams", "distill_params"> {
  let summary = "Parameters for a magic-state distillation protocol";
  let parameters = (ins
    "uint32_t" : $inputCount,    // number of noisy input states
    "uint32_t" : $outputCount    // number of output states (usually 1)
    // Error rates are stored as fixed-point parts-per-billion (uint64_t)
    // to avoid FieldParser<double> which MLIR 22 does not support.
  );
  let assemblyFormat = "`<` $inputCount `->` $outputCount `>`";
}

#endif // FTQC_ATTRS_TD
