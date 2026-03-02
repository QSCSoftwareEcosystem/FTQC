configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
    ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
)

# In a standalone (installed LLVM) build the LLVM support tools
# (FileCheck, count, not) are installed binaries, not CMake targets.
# Only list ftqc-opt (which we do build) as a build dependency.
set(FTQC_TEST_DEPENDS ftqc-opt)

add_lit_testsuite(check-ftqc "Running FTQC regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${FTQC_TEST_DEPENDS}
  ARGS -v
)
set_target_properties(check-ftqc PROPERTIES FOLDER "Tests")

add_subdirectory(Dialect)
add_subdirectory(Transforms)
add_subdirectory(Analysis)
add_subdirectory(Integration)
