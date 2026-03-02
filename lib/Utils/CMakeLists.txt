add_mlir_library(FTQCUtils
  StabilizerUtils.cpp
  PauliUtils.cpp
  SyndromeDecoder.cpp

  LINK_LIBS PUBLIC
    LLVMSupport
    MLIRSupport
)
