add_mlir_library(FTQCTransforms
  SteaneEncoding.cpp
  SurfaceCodeEncoding.cpp
  ColorCodeEncoding.cpp
  MagicStateDistillation.cpp
  SyndromeExtraction.cpp
  ErrorCorrection.cpp
  TransversalGates.cpp
  LowerToQuantum.cpp
  LowerToQASM3.cpp
  LowerToSTIM.cpp
  Passes.cpp

  DEPENDS
    MLIRFTQCPassesIncGen

  LINK_LIBS PUBLIC
    FTQCDialect
    FTQCUtils
    MLIRIR
    MLIRPass
    MLIRTransforms
    MLIRTransformUtils
    MLIRFuncDialect
    MLIRArithDialect
    MLIRSCFDialect
    MLIRDialectUtils
    MLIRRewrite
)
