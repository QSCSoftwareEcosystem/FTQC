cmake_minimum_required(VERSION 3.20)
project(FTQC VERSION 0.1.0 LANGUAGES C CXX)

#=======================================================================
# C++ Standard
#=======================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#=======================================================================
# Build type
#=======================================================================
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

#=======================================================================
# Find MLIR / LLVM
#
# Priority:
#   1. -DMLIR_DIR=<path>  (point to an MLIR build's cmake/mlir/ dir)
#   2. Well-known install prefixes
#=======================================================================

# Helper: allow users to point at an in-tree LLVM build.
if(DEFINED LLVM_BUILD_DIR AND NOT DEFINED MLIR_DIR)
  set(MLIR_DIR "${LLVM_BUILD_DIR}/lib/cmake/mlir")
  set(LLVM_DIR "${LLVM_BUILD_DIR}/lib/cmake/llvm")
endif()

# Point at llvm-lit from the llvm-project.c014 build if not overridden.
if(NOT DEFINED LLVM_EXTERNAL_LIT)
  set(LLVM_EXTERNAL_LIT
    "/home/nqx/Quantum/quantum-resources/llvm-project.c014/build/bin/llvm-lit"
    CACHE STRING "Path to the llvm-lit executable")
endif()

find_package(MLIR REQUIRED CONFIG)
message(STATUS "Found MLIR: ${MLIR_DIR}")
message(STATUS "MLIR version: ${MLIR_VERSION}")

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM: ${LLVM_DIR}")
message(STATUS "LLVM version: ${LLVM_VERSION}")

# Pull in standard MLIR and LLVM CMake modules.
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

#=======================================================================
# Include directories
#=======================================================================
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${MLIR_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)

# Suppress LLVM deprecation warnings in our code.
add_definitions(${LLVM_DEFINITIONS})

#=======================================================================
# LLVM tools binary directory (FileCheck, not, count, llvm-lit)
#
# When using an *installed* MLIR/LLVM, LLVM_TOOLS_DIR points to the
# install prefix bin/, which may not contain FileCheck.  Override this
# cache variable to point at the LLVM *build* bin/ that has those tools.
#
# Example:
#   cmake ... -DLLVM_TOOLS_BINARY_DIR=/path/to/llvm-project/build/bin
#=======================================================================
if(NOT DEFINED LLVM_TOOLS_BINARY_DIR)
  # Default: try the sibling build/bin of the install tree first.
  get_filename_component(_llvm_install_parent "${LLVM_BINARY_DIR}" DIRECTORY)
  set(_llvm_build_bin "${_llvm_install_parent}/build/bin")
  if(EXISTS "${_llvm_build_bin}/FileCheck")
    set(LLVM_TOOLS_BINARY_DIR "${_llvm_build_bin}"
        CACHE PATH "Directory containing LLVM FileCheck, not, count tools")
  else()
    set(LLVM_TOOLS_BINARY_DIR "${LLVM_TOOLS_DIR}"
        CACHE PATH "Directory containing LLVM FileCheck, not, count tools")
  endif()
endif()
message(STATUS "LLVM tools binary dir: ${LLVM_TOOLS_BINARY_DIR}")

#=======================================================================
# Options
#=======================================================================
option(FTQC_BUILD_TESTS     "Build the test suite"     ON)
option(FTQC_ENABLE_WARNINGS "Enable extra warnings"    ON)

if(FTQC_ENABLE_WARNINGS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  endif()
endif()

#=======================================================================
# TableGen targets for the FTQC dialect
#=======================================================================
add_subdirectory(include/FTQC/Dialect)
add_subdirectory(include/FTQC/Transforms)

#=======================================================================
# Library and executable targets
#=======================================================================
add_subdirectory(lib)
add_subdirectory(src)

#=======================================================================
# Tests
#=======================================================================
if(FTQC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

#=======================================================================
# Documentation (Doxygen)
#=======================================================================
find_package(Doxygen OPTIONAL_COMPONENTS dot)
if(DOXYGEN_FOUND)
  set(FTQC_DOXYFILE "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile")
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${FTQC_DOXYFILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
  message(STATUS "Doxygen found: run 'cmake --build <build_dir> --target docs'")
else()
  message(STATUS "Doxygen not found: 'docs' target unavailable")
endif()

#=======================================================================
# Install
#=======================================================================

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/setup.source
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMAND echo "SCRIPT_FILE=\${BASH_SOURCE[0]}" > ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "PYTHON_EXE=$(command -v python || command -v python3)" >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "if [[ -z \"\$PYTHON_EXE\" ]]; then" >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "    echo \"No Python is detected! Cannot source\" " >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "    return" >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "fi" >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "SCRIPT=$(\$PYTHON_EXE -c \"import os,sys; print(os.path.realpath(os.path.expanduser(sys.argv[1])))\" \${SCRIPT_FILE})" >> ${CMAKE_BINARY_DIR}/setup.source VERBATIM
  COMMAND echo "export FTQC=$(dirname -- \${SCRIPT})" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export PATH=.:\$FTQC/bin:\$PATH" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export LD_LIBRARY_PATH=.:\$FTQC/lib:\$FTQC/lib64:\$LD_LIBRARY_PATH" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export DYLD_LIBRARY_PATH=.:\$FTQC/default:\$FTQC/lib:\$DYLD_LIBRARY_PATH" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export CPATH=\$FTQC/include:\$CPATH" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export PYTHONPATH=\$FTQC:\$PYTHONPATH" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export SYSTEM=\$(hostname|cut -d . -f 1|sed 's/[0-9]*//g')" >>  ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "export MACHINE=$(echo \"\$SYSTEM\" | awk '{print toupper(substr(\$0,1,1)) tolower(substr(\$0,2))}')" >>  ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "echo \"*********************** FTQC Environent *********************\"" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "echo \"FTQC: \$FTQC\"" >> ${CMAKE_BINARY_DIR}/setup.source
  COMMAND echo "echo \"*************************************************************\"" >> ${CMAKE_BINARY_DIR}/setup.source
  )
add_custom_target(setup_source ALL DEPENDS ${CMAKE_BINARY_DIR}/setup.source)
install(FILES ${CMAKE_BINARY_DIR}/setup.source DESTINATION ${CMAKE_INSTALL_PREFIX})
install(DIRECTORY include/FTQC DESTINATION include)
install(TARGETS FTQCDialect FTQCTransforms FTQCAnalysis FTQCUtils
        EXPORT FTQCTargets
        LIBRARY  DESTINATION lib
        ARCHIVE  DESTINATION lib
        RUNTIME  DESTINATION bin)

install(EXPORT FTQCTargets
        FILE FTQCTargets.cmake
        NAMESPACE FTQC::
        DESTINATION lib/cmake/FTQC)
