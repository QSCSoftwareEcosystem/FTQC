//===- QPhysQubitReuse.cpp - Physical qubit reuse pass --------------------===//
//
// Renumbers physical qubits to use IDs in the range [0, max-qubits). Once this
// limit is reached, new allocations reuse a free ID from the least recently
// used qubit, so that as many operations can be performed in parallel as
// possible. This pass fails if the number of live qubits ever exceeds
// max-qubits; no attempt is currently made to reorder operations to get under
// the limit. No attempt is currently made to track qubits across blocks, so
// each function must have a single block and only use qubits allocated in that
// block.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCDialect.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Transforms/Passes.h"

#include "llvm/ADT/DenseMap.h"

#define GEN_PASS_DEF_FTQCQPHYSQUBITREUSEPASS
#include "FTQC/Transforms/Passes.h.inc"

#include <optional>
#include <queue>

using namespace mlir;
using namespace mlir::ftqc;

namespace {

struct FTQCQPhysQubitReusePassImpl
    : public ::impl::FTQCQPhysQubitReusePassBase<FTQCQPhysQubitReusePassImpl> {
  using Base::Base;

  std::optional<uint64_t> findQubitId(DenseMap<Value, uint64_t> &qubitIds,
                                      Value qubit, Operation *op) {
    auto it = qubitIds.find(qubit);
    if (it == qubitIds.end()) {
      op->emitError("ftqc-qphys-qubit-reuse: only qubits allocated in the "
                    "current block are currently supported");
      signalPassFailure();
      return std::nullopt;
    }
    return it->second;
  }

  void runOnOperation() override {
    auto func = getOperation();

    if (func.getBody().getBlocks().size() != 1) {
      func.emitError("ftqc-qphys-qubit-reuse: only functions with a single "
                     "block are currently supported");
      signalPassFailure();
      return;
    }

    DenseMap<Value, uint64_t> qubitIds;
    std::queue<uint64_t> freeIds;
    for (uint64_t i = 0; i < maxQubits; ++i)
      freeIds.push(i);

    for (Operation &op : func.getBody().front().getOperations()) {
      if (auto alloc = dyn_cast<QPhysAllocOp>(op)) {
        if (freeIds.empty()) {
          alloc.emitError("ftqc-qphys-qubit-reuse: qubit allocation would "
                          "exceed max-qubits");
          signalPassFailure();
          return;
        }
        alloc.setId(freeIds.front());
        freeIds.pop();
        qubitIds[alloc.getQubit()] = alloc.getId();
      } else if (isa<QPhysHOp, QPhysXOp, QPhysZOp, QPhysSOp, QPhysTOp,
                     QPhysResetOp>(op)) {
        auto id = findQubitId(qubitIds, op.getOperand(0), &op);
        if (!id)
          return;
        qubitIds[op.getResult(0)] = *id;
      } else if (isa<QPhysCXOp, QPhysCZOp, QPhysSwapOp>(op)) {
        auto id0 = findQubitId(qubitIds, op.getOperand(0), &op);
        auto id1 = findQubitId(qubitIds, op.getOperand(1), &op);
        if (!id0 || !id1)
          return;
        qubitIds[op.getResult(0)] = *id0;
        qubitIds[op.getResult(1)] = *id1;
      } else if (auto measure = dyn_cast<QPhysMeasureOp>(op)) {
        auto id = findQubitId(qubitIds, measure.getIn(), measure);
        if (!id)
          return;
        freeIds.push(*id);
      }
    }
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createQPhysQubitReusePass() {
  return std::make_unique<FTQCQPhysQubitReusePassImpl>();
}
