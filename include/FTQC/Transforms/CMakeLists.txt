# TableGen targets for FTQC passes

set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name FTQC)
add_public_tablegen_target(MLIRFTQCPassesIncGen)
