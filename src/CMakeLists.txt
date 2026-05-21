get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)

set(LLVM_LINK_COMPONENTS
  Core
  Support
  AsmParser
)

add_llvm_executable(ftqc-opt PARTIAL_SOURCES_INTENDED ftqc-opt.cpp)

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

#=======================================================================
# qasm3-import: standalone OpenQASM 2/3 → FTQC MLIR text converter.
# No MLIR/LLVM library dependencies — pure C++17 stdlib.
# Must use add_llvm_executable so LLVM's source-file checker accounts
# for qasm3-import.cpp in this directory.
#=======================================================================
set(LLVM_LINK_COMPONENTS)   # no LLVM libs needed
add_llvm_executable(qasm3-import qasm3-import.cpp)
llvm_update_compile_flags(qasm3-import)
install(TARGETS qasm3-import DESTINATION bin)
