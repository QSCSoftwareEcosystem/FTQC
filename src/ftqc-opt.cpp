//===- ftqc-opt.cpp - FTQC dialect optimizer driver -----------------------===//
//
// Main driver for the `ftqc-opt` tool.  Registers all FTQC dialects and
// passes, then hands off to the standard mlir-opt infrastructure.
//
// OpenQASM 3.0 input integration
// ───────────────────────────────
// When the input file has a .qasm extension, or when --import-qasm3 is given,
// the tool transparently runs the QASM → FTQC MLIR importer first and then
// applies the requested passes on the resulting IR.
//
// Usage:
//   ftqc-opt [options] [input.mlir | input.qasm | -]
//
// QASM-specific options (ignored for .mlir input):
//   --import-qasm3            Force QASM import even without .qasm extension
//   --qasm-ecc=<kind>         steane (default) | surface | color_code
//   --qasm-distance=<N>       code distance (default 3)
//   --qasm-func-name=<name>   MLIR function name (default: input filename stem)
//
// Examples:
//   ftqc-opt bell.qasm --ftqc-insert-syndrome --ftqc-resource-estimate
//   ftqc-opt --import-qasm3 --qasm-ecc=surface --qasm-distance=5 qft.qasm
//   ftqc-opt bell.mlir --ftqc-insert-syndrome          # normal MLIR path
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"
#include "QASM3Importer.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/InitAllDialects.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace mlir;

// ─── QASM import CLI options ─────────────────────────────────────────────────
// These are defined as global cl::opt so they are registered at static-init
// time and participate in the single ParseCommandLineOptions call inside
// registerAndParseCLIOptions().

static llvm::cl::opt<bool> ImportQASM3(
    "import-qasm3",
    llvm::cl::desc("Treat the input as OpenQASM 2/3 and import it to FTQC MLIR "
                   "(auto-enabled when the input file ends in .qasm)"),
    llvm::cl::init(false));

static llvm::cl::opt<std::string> QASMEcc(
    "qasm-ecc",
    llvm::cl::desc("Error-correcting code for QASM import: "
                   "steane (default), surface, color_code"),
    llvm::cl::value_desc("kind"),
    llvm::cl::init("steane"));

static llvm::cl::opt<int> QASMDistance(
    "qasm-distance",
    llvm::cl::desc("Code distance for QASM import (default: 3)"),
    llvm::cl::value_desc("N"),
    llvm::cl::init(3));

static llvm::cl::opt<std::string> QASMFuncName(
    "qasm-func-name",
    llvm::cl::desc("MLIR function name for QASM import "
                   "(default: stem of input filename)"),
    llvm::cl::value_desc("name"),
    llvm::cl::init(""));

// ─── Helpers ─────────────────────────────────────────────────────────────────

static bool hasQASMExtension(llvm::StringRef path) {
  return path.ends_with(".qasm") || path.ends_with(".qasm2") ||
         path.ends_with(".qasm3");
}

static std::string readFile(llvm::StringRef path, bool &ok) {
  if (path == "-") {
    std::ostringstream ss; ss << std::cin.rdbuf(); ok = true; return ss.str();
  }
  std::string pathStr = path.str();
  std::ifstream f(pathStr);
  if (!f) { ok = false; return {}; }
  std::ostringstream ss; ss << f.rdbuf(); ok = true; return ss.str();
}

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
  // Register all MLIR core dialects and FTQC dialect.
  DialectRegistry registry;
  registerAllDialects(registry);
  registry.insert<ftqc::FTQCDialect>();

  // Register all passes (core + FTQC).
  registerAllPasses();
  ftqc::registerFTQCPasses();

  // Register pipeline builders.
  PassPipelineRegistration<>(
      "ftqc-steane-pipeline",
      "Full Steane [[7,1,3]] fault-tolerant encoding pipeline",
      [](OpPassManager &pm) { ftqc::buildSteaneEncodePipeline(pm); });

  PassPipelineRegistration<>(
      "ftqc-surface-pipeline",
      "Full surface-code fault-tolerant encoding pipeline",
      [](OpPassManager &pm) { ftqc::buildSurfaceEncodePipeline(pm); });

  PassPipelineRegistration<>(
      "ftqc-distill-pipeline",
      "15-to-1 T state distillation pipeline",
      [](OpPassManager &pm) { ftqc::buildDistillationPipeline(pm); });

  // ── Parse ALL CLI options (our QASM opts + standard mlir-opt opts) ────────
  // registerAndParseCLIOptions() registers MlirOptMainConfig options, our
  // global cl::opts above, and then calls ParseCommandLineOptions().
  // It returns the (inputFilename, outputFilename) positional args.
  auto [inputFilename, outputFilename] =
      mlir::registerAndParseCLIOptions(argc, argv, "FTQC dialect optimizer\n",
                                       registry);

  // ── Decide whether to import QASM ────────────────────────────────────────
  bool doImport = ImportQASM3 || hasQASMExtension(inputFilename);

  if (!doImport) {
    // Normal MLIR path — input is already MLIR text / bytecode.
    return mlir::asMainReturnCode(
        mlir::MlirOptMain(argc, argv, inputFilename, outputFilename, registry));
  }

  // ── QASM import path ──────────────────────────────────────────────────────

  // Validate ECC option.
  std::string eccKind = QASMEcc;
  if (eccKind != "steane" && eccKind != "surface" && eccKind != "color_code") {
    llvm::errs() << "ftqc-opt: --qasm-ecc must be steane, surface, or color_code\n";
    return EXIT_FAILURE;
  }

  // Determine function name.
  std::string funcName = QASMFuncName;
  if (funcName.empty())
    funcName = (inputFilename == "-") ? "circuit"
                                      : qasm3import::stemOf(inputFilename);

  // Read the QASM source.
  bool ok = false;
  std::string qasmSrc = readFile(inputFilename, ok);
  if (!ok) {
    llvm::errs() << "ftqc-opt: cannot open '" << inputFilename << "'\n";
    return EXIT_FAILURE;
  }

  // Run the importer → MLIR text.
  std::string mlirText =
      qasm3import::importQASM3(qasmSrc, eccKind, QASMDistance, funcName);

  // Wrap in a MemoryBuffer and run the pass pipeline.
  auto buffer = llvm::MemoryBuffer::getMemBufferCopy(mlirText, inputFilename);

  // Open the output stream.
  std::string errMsg;
  auto output = mlir::openOutputFile(outputFilename.empty() ? "-" : outputFilename,
                                     &errMsg);
  if (!output) {
    llvm::errs() << "ftqc-opt: " << errMsg << "\n";
    return EXIT_FAILURE;
  }

  // Build config from the already-parsed CL options (includes pass pipeline).
  auto config = MlirOptMainConfig::createFromCLOptions();

  auto result =
      mlir::MlirOptMain(output->os(), std::move(buffer), registry, config);
  output->keep();
  return mlir::asMainReturnCode(result);
}
