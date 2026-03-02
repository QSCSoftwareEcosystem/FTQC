#===- AddFTQC.cmake - Helper macros for building FTQC components ----------===#
#
# Convenience wrappers around the MLIR/LLVM CMake infrastructure for use
# inside this project.
#
#===-----------------------------------------------------------------------===#

# add_ftqc_library(name [SHARED|STATIC] sources...)
#   Thin wrapper over add_mlir_library that also links common FTQC deps and
#   sets up include paths.
function(add_ftqc_library name)
  cmake_parse_arguments(ARG "SHARED;STATIC;EXCLUDE_FROM_ALL" "" "ADDITIONAL_HEADERS;DEPENDS;LINK_LIBS" ${ARGN})

  set(_lib_type "")
  if(ARG_SHARED)
    set(_lib_type SHARED)
  elseif(ARG_STATIC)
    set(_lib_type STATIC)
  endif()

  add_mlir_library(${name}
    ${_lib_type}
    ${ARG_UNPARSED_ARGUMENTS}
    ADDITIONAL_HEADERS ${ARG_ADDITIONAL_HEADERS}
    DEPENDS           ${ARG_DEPENDS}
    LINK_LIBS         ${ARG_LINK_LIBS}
  )

  target_include_directories(${name} PUBLIC
    $<BUILD_INTERFACE:${FTQC_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${FTQC_BUILD_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  )
endfunction()

# add_ftqc_executable(name sources...)
#   Wrapper over add_llvm_executable that links FTQC and MLIR opt support.
function(add_ftqc_executable name)
  add_llvm_executable(${name} ${ARGN})

  target_include_directories(${name} PRIVATE
    ${FTQC_INCLUDE_DIR}
    ${FTQC_BUILD_INCLUDE_DIR}
  )
endfunction()

# add_ftqc_dialect_library(name sources...)
#   Builds a dialect library and adds the standard MLIR dialect dependencies.
function(add_ftqc_dialect_library name)
  add_ftqc_library(${name}
    ${ARGN}
    LINK_LIBS
      MLIRIR
      MLIRParser
      MLIRSupport
  )
endfunction()

# ftqc_tablegen(output_file tablegen_flags)
#   Runs mlir-tblgen on the current TableGen source file.
macro(ftqc_tablegen ofn)
  mlir_tablegen(${ofn} ${ARGN})
endmacro()

# add_ftqc_subdirectory_if_lit(subdir)
#   Conditionally adds a subdirectory to lit if LLVM_BUILD_TESTS is ON.
function(add_subdirectory_if_exists_lit subdir)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${subdir}/CMakeLists.txt)
    add_subdirectory(${subdir})
  endif()
endfunction()
