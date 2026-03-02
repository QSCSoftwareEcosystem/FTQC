//===- Passes.cpp - FTQC pass registration and pipeline builders ----------===//
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassRegistry.h"

using namespace mlir;
using namespace mlir::ftqc;

// Register all FTQC passes.  We bypass GEN_PASS_REGISTRATION because our
// factory functions use short names (createSteaneEncodePass, etc.) while
// the generated infrastructure expects the prefixed createFTQCXxx names.
void mlir::ftqc::registerFTQCPasses() {
  ::mlir::registerPass([] { return createSteaneEncodePass(); });
  ::mlir::registerPass([] { return createSurfaceEncodePass(); });
  ::mlir::registerPass([] { return createColorCodeEncodePass(); });
  ::mlir::registerPass([] { return createInsertSyndromePass(); });
  ::mlir::registerPass([] { return createInsertCorrectionPass(); });
  ::mlir::registerPass([] { return createPauliFrameOptPass(); });
  ::mlir::registerPass([] { return createMagicDistillPass(); });
  ::mlir::registerPass([] { return createLowerToQuantumPass(); });
  ::mlir::registerPass([] { return createTransversalGatePass(); });
  ::mlir::registerPass([] { return createResourceEstimatePass(); });
  ::mlir::registerPass([] { return createFaultPathAnalysisPass(); });
  ::mlir::registerPass([] { return createThresholdAnalysisPass(); });
}

void mlir::ftqc::buildSteaneEncodePipeline(OpPassManager &pm,
                                            unsigned distance,
                                            bool insertEC,
                                            const std::string &synMethod) {
  // Module-level encoding.
  pm.addPass(createSteaneEncodePass(distance, insertEC, 3));
  // Function-level syndrome + correction.
  auto &funcPM = pm.nest<func::FuncOp>();
  if (insertEC) {
    funcPM.addPass(createInsertSyndromePass(synMethod, 3));
    funcPM.addPass(createInsertCorrectionPass());
  }
  // Transversal gate verification.
  funcPM.addPass(createTransversalGatePass(true));
  // Resource estimate at module level.
  pm.addPass(createResourceEstimatePass(false));
}

void mlir::ftqc::buildSurfaceEncodePipeline(OpPassManager &pm,
                                             unsigned distance,
                                             bool useLattice) {
  pm.addPass(createSurfaceEncodePass(distance, useLattice));
  auto &funcPM = pm.nest<func::FuncOp>();
  funcPM.addPass(createInsertSyndromePass("steane", 3));
  funcPM.addPass(createInsertCorrectionPass());
  pm.addPass(createResourceEstimatePass(false));
}

void mlir::ftqc::buildDistillationPipeline(OpPassManager &pm,
                                            const std::string &protocol,
                                            unsigned levels) {
  auto &funcPM = pm.nest<func::FuncOp>();
  funcPM.addPass(createMagicDistillPass(protocol, levels, 1e-10));
}
