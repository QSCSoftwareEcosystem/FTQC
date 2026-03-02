//===- ftqc-opt.cpp - FTQC dialect optimizer driver -----------------------===//
//
// Main driver for the `ftqc-opt` tool.  Registers all FTQC dialects and
// passes, then hands off to the standard mlir-opt infrastructure.
//
// Usage:
//   ftqc-opt [options] [input.mlir]
//
// Example:
//   ftqc-opt --ftqc-steane-encode --ftqc-insert-syndrome \
//            --ftqc-insert-correction --ftqc-resource-estimate \
//            input.mlir -o output.mlir
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

using namespace mlir;

int main(int argc, char **argv) {
  // Register all MLIR core dialects.
  DialectRegistry registry;
  registerAllDialects(registry);

  // Register the FTQC dialect.
  registry.insert<ftqc::FTQCDialect>();

  // Register all MLIR core passes.
  registerAllPasses();

  // Register all FTQC passes.
  ftqc::registerFTQCPasses();

  // Register pipeline builders.
  PassPipelineRegistration<>(
      "ftqc-steane-pipeline",
      "Full Steane [[7,1,3]] fault-tolerant encoding pipeline",
      [](OpPassManager &pm) {
        ftqc::buildSteaneEncodePipeline(pm);
      });

  PassPipelineRegistration<>(
      "ftqc-surface-pipeline",
      "Full surface-code fault-tolerant encoding pipeline",
      [](OpPassManager &pm) {
        ftqc::buildSurfaceEncodePipeline(pm);
      });

  PassPipelineRegistration<>(
      "ftqc-distill-pipeline",
      "15-to-1 T state distillation pipeline",
      [](OpPassManager &pm) {
        ftqc::buildDistillationPipeline(pm);
      });

  return mlir::asMainReturnCode(
      mlir::MlirOptMain(argc, argv, "FTQC dialect optimizer\n", registry));
}
