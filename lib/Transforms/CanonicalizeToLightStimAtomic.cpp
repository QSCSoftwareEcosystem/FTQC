//===- CanonicalizeToLightStimAtomic.cpp ---------------------------------===//
//
// FTQC -> LightStim atomic timeline canonicalization.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Transforms/Passes.h"

#include "LightStimAtomicEmitter.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#define GEN_PASS_DEF_FTQCCANONICALIZETOLIGHTSTIMATOMICPASS
#include "FTQC/Transforms/Passes.h.inc"

#include <system_error>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCCanonicalizeToLightStimAtomicPassImpl
    : public ::impl::FTQCCanonicalizeToLightStimAtomicPassBase<
          FTQCCanonicalizeToLightStimAtomicPassImpl> {
  using FTQCCanonicalizeToLightStimAtomicPassBase::FTQCCanonicalizeToLightStimAtomicPassBase;

  void runOnOperation() override {
    auto mod = getOperation();
    lsatomic::LightStimAtomicEmitter emitter(lightstimCode, patchSpacing,
                         defaultSERounds, surgeryRounds);

    auto json = emitter.emitAtomicIR(mod);
    if (failed(json)) {
      signalPassFailure();
      return;
    }

    if (!outputFile.empty()) {
      std::error_code ec;
      llvm::raw_fd_ostream file(outputFile, ec);
      if (ec) {
        mod.emitError("ftqc-canonicalize-to-lightstim-atomic: cannot open output file '")
            << outputFile << "': " << ec.message();
        signalPassFailure();
        return;
      }
      file << *json;
    } else {
      llvm::errs() << *json;
    }

    if (attachModuleAttr)
      mod->setAttr("ftqc.lightstim_atomic_ir",
                   StringAttr::get(mod.getContext(), *json));
  }
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::ftqc::createCanonicalizeToLightStimAtomicPass() {
  return std::make_unique<FTQCCanonicalizeToLightStimAtomicPassImpl>();
}
