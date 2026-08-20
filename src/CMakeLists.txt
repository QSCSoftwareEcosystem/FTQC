get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)

set(LLVM_LINK_COMPONENTS
  Core
  Support
  AsmParser
)

set(REFERENCE_LLVM_VERSION "22.0.0")

#=======================================================================
# libftqc_capi: IRIS kernel C-API shared library.
#
# Exposes two entry points for use as IRIS task kernels:
#   ftqc_iris_mlir_opt  -- run a pass pipeline on an in-memory MLIR buffer
#   ftqc_iris_qasm_opt  -- import OpenQASM 2/3 from memory, then run a pipeline
#
# Neither function uses llvm::cl or touches the filesystem, so both are safe
# to call from concurrent IRIS task graphs.
#=======================================================================
add_mlir_library(ftqc_capi SHARED PARTIAL_SOURCES_INTENDED
  ftqc_capi.cpp

  LINK_LIBS PUBLIC
    FTQCDialect
    FTQCTransforms
    FTQCAnalysis
    FTQCUtils
    ${dialect_libs}
    ${conversion_libs}
    MLIRIR
    MLIRPass
    MLIRParser
    MLIRSupport
    MLIRTransforms
    MLIRFuncDialect
    MLIRArithDialect
    MLIRSCFDialect
    MLIRBytecodeWriter
)

# QASM3Importer.h lives alongside ftqc_capi.cpp in src/.
target_include_directories(ftqc_capi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The importer uses exceptions to abandon malformed input lines.  Override
# LLVM's default -fno-exceptions without enabling RTTI (LLVM was built without
# RTTI, so enabling it here would cause missing typeinfo symbols at link time).
target_compile_options(ftqc_capi PRIVATE -fexceptions)

set_target_properties(ftqc_capi PROPERTIES
  OUTPUT_NAME ftqc
  VERSION     1.0.0
  SOVERSION   1
)

# registerAllDialects / registerAllPasses moved to separate libs in LLVM >= 22.
if("${LLVM_PACKAGE_VERSION}" VERSION_GREATER_EQUAL "${REFERENCE_LLVM_VERSION}")
  target_link_libraries(ftqc_capi PUBLIC
    MLIRRegisterAllDialects
    MLIRRegisterAllPasses
  )
endif()

install(FILES ${CMAKE_SOURCE_DIR}/include/FTQC/ftqc_capi.h
  DESTINATION include/FTQC
)

add_llvm_executable(ftqc-opt PARTIAL_SOURCES_INTENDED ftqc-opt.cpp)

llvm_update_compile_flags(ftqc-opt)
target_compile_options(ftqc-opt PRIVATE -fexceptions)

# Reuse all MLIR/FTQC deps transitively through ftqc_capi; only add
# MLIROptLib which is specific to the ftqc-opt driver (not needed in the lib).
target_link_libraries(ftqc-opt PRIVATE
  ftqc_capi
  MLIROptLib
)

# MLIRRegisterAllDialects and MLIRRegisterAllPasses flow transitively from
# ftqc_capi (PUBLIC), so no extra link needed here for LLVM >= 22.

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
target_compile_options(qasm3-import PRIVATE -fexceptions)
install(TARGETS qasm3-import DESTINATION bin)
