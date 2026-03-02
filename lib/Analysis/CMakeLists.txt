add_mlir_library(FTQCAnalysis
  ResourceEstimation.cpp
  ThresholdAnalysis.cpp
  FaultPathAnalysis.cpp

  LINK_LIBS PUBLIC
    FTQCDialect
    FTQCUtils
    MLIRIR
    MLIRPass
    MLIRFuncDialect
    MLIRSupport
)
