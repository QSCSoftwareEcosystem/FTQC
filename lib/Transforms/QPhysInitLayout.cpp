//===- QPhysInitLayout.cpp - Apply an initial hardware qubit layout -------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Transforms/Passes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#define GEN_PASS_DEF_FTQCQPHYSINITLAYOUTPASS
#include "FTQC/Transforms/Passes.h.inc"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace mlir;
using namespace mlir::ftqc;

namespace {
struct FTQCQPhysInitLayoutPassImpl
    : public ::impl::FTQCQPhysInitLayoutPassBase<FTQCQPhysInitLayoutPassImpl> {
  using Base::Base;

  void runOnOperation() override {
    if (inputFile.empty())
      return;

    ModuleOp module = getOperation();
    auto buffer = llvm::MemoryBuffer::getFile(inputFile);
    if (!buffer) {
      module.emitError("ftqc-qphys-init-layout: cannot open input file '")
          << inputFile << "': " << buffer.getError().message();
      signalPassFailure();
      return;
    }

    llvm::Expected<llvm::json::Value> parsed =
        llvm::json::parse(buffer.get()->getBuffer());
    if (!parsed) {
      module.emitError("ftqc-qphys-init-layout: invalid JSON in '")
          << inputFile << "': " << llvm::toString(parsed.takeError());
      signalPassFailure();
      return;
    }

    llvm::json::Object *root = parsed->getAsObject();
    llvm::json::Object *mapping =
        root ? root->getObject("logical_to_hardware_mapping") : nullptr;
    if (!mapping) {
      module.emitError("ftqc-qphys-init-layout: JSON file '")
          << inputFile
          << "' must contain an object named 'logical_to_hardware_mapping'";
      signalPassFailure();
      return;
    }

    llvm::DenseMap<QPhysAllocOp, int64_t> updates;
    WalkResult result = module.walk([&](QPhysAllocOp alloc) {
      int64_t logicalID = alloc.getId();
      std::optional<int64_t> hardwareID =
          mapping->getInteger(std::to_string(logicalID));
      if (!hardwareID) {
        alloc.emitError("ftqc-qphys-init-layout: missing or non-integer "
                        "mapping for logical qubit id ")
            << logicalID;
        return WalkResult::interrupt();
      }
      updates.try_emplace(alloc, *hardwareID);
      return WalkResult::advance();
    });
    if (result.wasInterrupted()) {
      signalPassFailure();
      return;
    }
    for (auto [alloc, hardwareID] : updates)
      alloc.setId(hardwareID);
  }
};
} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysInitLayoutPass() {
  return std::make_unique<FTQCQPhysInitLayoutPassImpl>();
}
