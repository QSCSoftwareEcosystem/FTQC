//===- LowerToLightStimAtomicJSON.cpp ------------------------------------===//
//
// FTQC -> LightStim atomic descriptor JSON lowering.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Transforms/Passes.h"

#include "LightStimAtomicEmitter.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCLOWERTOLIGHTSTIMATOMICJSONPASS
#include "FTQC/Transforms/Passes.h.inc"

#include <system_error>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCLowerToLightStimAtomicJSONPassImpl
    : public ::impl::FTQCLowerToLightStimAtomicJSONPassBase<
          FTQCLowerToLightStimAtomicJSONPassImpl> {
  using FTQCLowerToLightStimAtomicJSONPassBase::FTQCLowerToLightStimAtomicJSONPassBase;

  void runOnOperation() override {
    auto mod = getOperation();
    lsatomic::LightStimAtomicEmitter emitter(lightstimCode, patchSpacing,
                         defaultSERounds, surgeryRounds);

    lsatomic::AtomicDescriptorConfig cfg;
    cfg.physErrorRate = physErrorRate;
    cfg.decoderName = decoderName;
    cfg.decoderBackend = decoderBackend;
    cfg.maxErrors = maxErrors;
    cfg.maxShots = maxShots;
    cfg.batchSize = batchSize;
    cfg.numWorkers = numWorkers;
    cfg.noiselessCheckShots = noiselessCheckShots;

    auto json = emitter.emitAtomicDescriptor(mod, cfg);
    if (failed(json)) {
      signalPassFailure();
      return;
    }

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-lower-to-lightstim-atomic-json: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << *json;
    } else {
      llvm::errs() << *json;
    }

    if (attachModuleAttr)
      mod->setAttr("ftqc.lightstim_atomic_json",
                   StringAttr::get(mod.getContext(), *json));
  }
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::ftqc::createLowerToLightStimAtomicJSONPass() {
  return std::make_unique<FTQCLowerToLightStimAtomicJSONPassImpl>();
}
