get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)

set(LLVM_LINK_COMPONENTS
  Core
  Support
  AsmParser
)

add_llvm_executable(ftqc-opt ftqc-opt.cpp)

llvm_update_compile_flags(ftqc-opt)

target_link_libraries(ftqc-opt PRIVATE
  FTQCDialect
  FTQCTransforms
  FTQCAnalysis
  FTQCUtils
  ${dialect_libs}
  ${conversion_libs}
  MLIRRegisterAllDialects
  MLIRRegisterAllPasses
  MLIROptLib
  MLIRIR
  MLIRPass
  MLIRSupport
  MLIRTransforms
  MLIRFuncDialect
  MLIRArithDialect
  MLIRSCFDialect
  MLIRParser
  MLIRBytecodeWriter
)

install(TARGETS ftqc-opt DESTINATION bin)
