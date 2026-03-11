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

set(REFERENCE_LLVM_VERSION "22.0.0")
if("${LLVM_PACKAGE_VERSION}" VERSION_GREATER_EQUAL "${REFERENCE_LLVM_VERSION}")
	target_link_libraries(ftqc-opt PRIVATE
	  MLIRRegisterAllDialects
	  MLIRRegisterAllPasses
	)
endif()

install(TARGETS ftqc-opt DESTINATION bin)
