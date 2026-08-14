# TableGen targets for the FTQC dialect

set(LLVM_TARGET_DEFINITIONS FTQCDialect.td)
mlir_tablegen(FTQCDialect.h.inc   -gen-dialect-decls    -dialect=ftqc)
mlir_tablegen(FTQCDialect.cpp.inc -gen-dialect-defs     -dialect=ftqc)
add_public_tablegen_target(MLIRFTQCDialectIncGen)

set(LLVM_TARGET_DEFINITIONS FTQCAttrs.td)
mlir_tablegen(FTQCEnums.h.inc            -gen-enum-decls)
mlir_tablegen(FTQCEnums.cpp.inc          -gen-enum-defs)
mlir_tablegen(FTQCAttrInterfaces.h.inc   -gen-attr-interface-decls)
mlir_tablegen(FTQCAttrInterfaces.cpp.inc -gen-attr-interface-defs)
mlir_tablegen(FTQCAttrs.h.inc            -gen-attrdef-decls    -attrdefs-dialect=ftqc)
mlir_tablegen(FTQCAttrs.cpp.inc          -gen-attrdef-defs     -attrdefs-dialect=ftqc)
add_public_tablegen_target(MLIRFTQCAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS FTQCTypes.td)
mlir_tablegen(FTQCTypes.h.inc     -gen-typedef-decls    -typedefs-dialect=ftqc)
mlir_tablegen(FTQCTypes.cpp.inc   -gen-typedef-defs     -typedefs-dialect=ftqc)
add_public_tablegen_target(MLIRFTQCTypesIncGen)

set(LLVM_TARGET_DEFINITIONS FTQCOps.td)
mlir_tablegen(FTQCOps.h.inc       -gen-op-decls)
mlir_tablegen(FTQCOps.cpp.inc     -gen-op-defs)
add_public_tablegen_target(MLIRFTQCOpsIncGen)
