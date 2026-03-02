add_mlir_library(FTQCDialect
  FTQCDialect.cpp
  FTQCAttrs.cpp
  FTQCTypes.cpp
  FTQCOps.cpp

  DEPENDS
    MLIRFTQCDialectIncGen
    MLIRFTQCAttrsIncGen
    MLIRFTQCTypesIncGen
    MLIRFTQCOpsIncGen

  LINK_LIBS PUBLIC
    MLIRIR
    MLIRDialect
    MLIRSupport
    MLIRFuncDialect
)
