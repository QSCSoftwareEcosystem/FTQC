//===- ExpandPhysical.cpp - Logical -> physical (qphys) expansion ---------===//
//
// Unfolds each `!ftqc.logical_qubit` into its physical block of
// `!ftqc.qphys_qubit` values and rewrites logical gates / syndrome ops into
// `ftqc.qphys.*` physical gate + measure circuits.  Data and ancilla qubits
// become explicit, labeled SSA values (role = "data" / "x_anc" / "z_anc").
//
// Scope: Steane [[7,1,3]] Clifford + syndrome + measurement subset.  Logical
// ops that require magic states or other codes (T, Toffoli, distillation,
// surface/color, correction, Pauli frames) are intentionally *not* expanded —
// the pass errors out rather than emit physically wrong or dangling IR.
//
//===----------------------------------------------------------------------===//

#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// Steane [[7,1,3]] layout constants (mirrors LowerToSTIM.cpp)
//===----------------------------------------------------------------------===//

namespace {

// Stabilizer supports (data-qubit offsets within a block); Steane is self-dual
// so the same three supports serve both X and Z stabilizers.
static const int STEANE_STABS[3][4] = {
    {0, 2, 4, 6},
    {1, 2, 5, 6},
    {3, 4, 5, 6},
};
static constexpr int STEANE_DATA = 7;
static constexpr int STEANE_ANC = 3;

using Block = SmallVector<Value, STEANE_DATA + STEANE_ANC>;

struct FTQCExpandPhysicalPassImpl
    : public PassWrapper<FTQCExpandPhysicalPassImpl,
                         OperationPass<func::FuncOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCExpandPhysicalPassImpl)

  SmallVector<Value> initializedAncilla; // Ancilla qubits that are already in |0> state

  StringRef getArgument() const override { return "ftqc-expand-physical"; }
  StringRef getDescription() const override {
    return "Unfold logical qubits into physical qphys data + ancilla qubits";
  }

  // --- physical-op builders ------------------------------------------------
  Type qType(OpBuilder &b) { return b.getType<QPhysQubitType>(); }

  Value alloc(OpBuilder &b, Location loc, int id, StringRef role) {
    return b.create<QPhysAllocOp>(loc, qType(b), b.getI64IntegerAttr(id),
                                  b.getStringAttr(role))
        .getQubit();
  }
  Value reset(OpBuilder &b, Location loc, Value q) {
    return b.create<QPhysResetOp>(loc, q.getType(), q).getOut();
  }
  Value h(OpBuilder &b, Location loc, Value q) {
    return b.create<QPhysHOp>(loc, q.getType(), q).getOut();
  }
  Value x(OpBuilder &b, Location loc, Value q) {
    return b.create<QPhysXOp>(loc, q.getType(), q).getOut();
  }
  Value z(OpBuilder &b, Location loc, Value q) {
    return b.create<QPhysZOp>(loc, q.getType(), q).getOut();
  }
  Value s(OpBuilder &b, Location loc, Value q) {
    return b.create<QPhysSOp>(loc, q.getType(), q).getOut();
  }
  // CNOT: returns {controlOut, targetOut}.
  std::pair<Value, Value> cx(OpBuilder &b, Location loc, Value c, Value t) {
    auto op = b.create<QPhysCXOp>(loc, qType(b), qType(b), c, t);
    return {op.getResult(0), op.getResult(1)};
  }
  std::pair<Value, Value> cz(OpBuilder &b, Location loc, Value a, Value c) {
    auto op = b.create<QPhysCZOp>(loc, qType(b), qType(b), a, c);
    return {op.getResult(0), op.getResult(1)};
  }
  // Measure with optional QEC-structure metadata (role / stabilizer id / round
  // / logical-qubit index) so a physical back end can reconstruct detectors and
  // the logical observable without re-deriving the code structure.
  Value measure(OpBuilder &b, Location loc, Value q, StringRef key,
                StringRef role = "", int64_t stab = -1, int64_t round = -1,
                int64_t logical = -1) {
    StringAttr keyAttr   = b.getStringAttr(key);
    StringAttr roleAttr  = role.empty() ? StringAttr() : b.getStringAttr(role);
    IntegerAttr stabAttr  = stab  < 0 ? IntegerAttr() : b.getI64IntegerAttr(stab);
    IntegerAttr roundAttr = round < 0 ? IntegerAttr() : b.getI64IntegerAttr(round);
    IntegerAttr logAttr = logical < 0 ? IntegerAttr() : b.getI64IntegerAttr(logical);
    return b.create<QPhysMeasureOp>(loc, b.getI1Type(), q, keyAttr, roleAttr,
                                    stabAttr, roundAttr, logAttr)
        .getBit();
  }

  // Stable stabilizer id: 6 stabilizers per logical qubit (X:0-2, Z:3-5), so
  // the same stabilizer of the same logical qubit keeps its id across rounds.
  static int64_t stabId(int logical, int local, bool zType) {
    return (int64_t)logical * 6 + (zType ? 3 : 0) + local;
  }

  // --- syndrome-extraction subcircuits (mutate the data block in place) -----
  void xSyndrome(OpBuilder &b, Location loc, Block &data, int &nextId,
                 int &ancIndex, int logical,
                 llvm::DenseMap<int64_t, int> &stabRound) {
    int local = 0;
    for (auto &stab : STEANE_STABS) {
      Value anc = data[STEANE_DATA + ancIndex];  // Use pre-allocated ancilla qubits
      if (!llvm::is_contained(initializedAncilla, anc)) {
        anc = reset(b, loc, anc);
      } else {
        auto it = llvm::find(initializedAncilla, anc);
        initializedAncilla.erase(it);
      }
      anc = h(b, loc, anc);
      for (int t : stab) {          // X-stab: ancilla controls the data
        auto [ao, dto] = cx(b, loc, anc, data[t]);
        anc = ao;
        data[t] = dto;
      }
      anc = h(b, loc, anc);
      data[STEANE_DATA + ancIndex] = anc; // Store back the updated ancilla
      if( ++ancIndex >= STEANE_ANC) ancIndex = 0; // wrap around if we exceed the number of ancilla
      int64_t sid = stabId(logical, local, /*zType=*/false);
      (void)measure(b, loc, anc, "sx", "x_anc", sid, stabRound[sid]++, logical);
      ++local;
    }
  }
  void zSyndrome(OpBuilder &b, Location loc, Block &data, int &nextId,
                 int &ancIndex, int logical,
                 llvm::DenseMap<int64_t, int> &stabRound) {
    int local = 0;
    for (auto &stab : STEANE_STABS) {
      Value anc = data[STEANE_DATA + ancIndex];  // Use pre-allocated ancilla qubits
      if (!llvm::is_contained(initializedAncilla, anc)) {
        anc = reset(b, loc, anc);
      } else {
        auto it = llvm::find(initializedAncilla, anc);
        initializedAncilla.erase(it);
      }
      for (int t : stab) {          // Z-stab: data controls the ancilla
        auto [dto, ao] = cx(b, loc, data[t], anc);
        data[t] = dto;
        anc = ao;
      }
      data[STEANE_DATA + ancIndex] = anc; // Store back the updated ancilla
      if( ++ancIndex >= STEANE_ANC) ancIndex = 0; // wrap around if we exceed the number of ancilla
      int64_t sid = stabId(logical, local, /*zType=*/true);
      (void)measure(b, loc, anc, "sz", "z_anc", sid, stabRound[sid]++, logical);
      ++local;
    }
  }

  void runOnOperation() override {
    func::FuncOp func = getOperation();
    MLIRContext *ctx = &getContext();

    // logical-qubit SSA value  ->  its physical data-qubit block.
    llvm::DenseMap<Value, Block> blocks;
    // logical-qubit SSA value  ->  its physical ancilla-qubit index.
    llvm::DenseMap<Value, int> ancIndices;
    // logical-qubit SSA value  ->  its logical-qubit index (0, 1, ...).
    llvm::DenseMap<Value, int> logIdx;
    int logCounter = 0;
    // stabilizer id  ->  next syndrome round (for detector metadata).
    llvm::DenseMap<int64_t, int> stabRound;
    int nextId = 0;
    SmallVector<Operation *> toErase;
    bool failed = false;

    // Single structured region assumed (func entry block).
    for (Operation &opRef : llvm::make_early_inc_range(
             func.getBody().front().getOperations())) {
      Operation *op = &opRef;
      OpBuilder b(op);
      Location loc = op->getLoc();

      auto mapResultBlock = [&](Value logicalResult, Block blk) {
        blocks[logicalResult] = std::move(blk);
      };

      if (auto init = dyn_cast<InitZeroOp>(op)) {
        Block data;
        for (int i = 0; i < STEANE_DATA; ++i)
          data.push_back(alloc(b, loc, nextId++, "data"));
        for (int i = 0; i < STEANE_ANC; ++i) {
          auto anc = alloc(b, loc, nextId++, "anc");
          data.push_back(anc);
          initializedAncilla.push_back(anc);
        }
        // |0_L> encoder: H on each stabilizer check qubit, then CX to support.
        for (auto &stab : STEANE_STABS) {
          int chk = stab[0];
          data[chk] = h(b, loc, data[chk]);
          for (int t : stab)
            if (t != chk) {
              auto [co, to] = cx(b, loc, data[chk], data[t]);
              data[chk] = co;
              data[t] = to;
            }
        }
        mapResultBlock(init.getResult(), std::move(data));
        ancIndices[init.getResult()] = 0; // Start ancilla index at 0 for this logical qubit
        logIdx[init.getResult()] = logCounter++;
        toErase.push_back(op);
      } else if (auto init = dyn_cast<InitPlusOp>(op)) {
        Block data;
        for (int i = 0; i < STEANE_DATA; ++i)
          data.push_back(alloc(b, loc, nextId++, "data"));
        for (int i = 0; i < STEANE_ANC; ++i) {
          auto anc = alloc(b, loc, nextId++, "anc");
          data.push_back(anc);
          initializedAncilla.push_back(anc);
        }
        for (auto &stab : STEANE_STABS) {
          int chk = stab[0];
          data[chk] = h(b, loc, data[chk]);
          for (int t : stab)
            if (t != chk) {
              auto [co, to] = cx(b, loc, data[chk], data[t]);
              data[chk] = co;
              data[t] = to;
            }
        }
        for (auto &q : data) q = h(b, loc, q); // |+_L> = transversal H
        mapResultBlock(init.getResult(), std::move(data));
        ancIndices[init.getResult()] = 0; // Start ancilla index at 0 for this logical qubit
        logIdx[init.getResult()] = logCounter++;
        toErase.push_back(op);
      } else if (isa<LogicalHOp, LogicalXOp, LogicalZOp, LogicalSOp,
                     LogicalYOp>(op)) {
        Block blk = blocks.lookup(op->getOperand(0));
        for(int i = 0; i < STEANE_DATA; ++i) {
          auto &q = blk[i];
          if (isa<LogicalHOp>(op)) q = h(b, loc, q);
          else if (isa<LogicalXOp>(op)) q = x(b, loc, q);
          else if (isa<LogicalZOp>(op)) q = z(b, loc, q);
          else if (isa<LogicalSOp>(op)) q = s(b, loc, q);
          else { q = x(b, loc, q); q = z(b, loc, q); } // Y = XZ (up to phase)
        }
        mapResultBlock(op->getResult(0), std::move(blk));
        logIdx[op->getResult(0)] = logIdx.lookup(op->getOperand(0));
        toErase.push_back(op);
      } else if (isa<LogicalCNOTOp, LogicalCZOp>(op)) {
        Block a = blocks.lookup(op->getOperand(0));
        Block c = blocks.lookup(op->getOperand(1));
        for (int i = 0; i < STEANE_DATA; ++i) {
          if (isa<LogicalCNOTOp>(op)) {
            auto [ao, co] = cx(b, loc, a[i], c[i]);
            a[i] = ao; c[i] = co;
          } else {
            auto [ao, co] = cz(b, loc, a[i], c[i]);
            a[i] = ao; c[i] = co;
          }
        }
        mapResultBlock(op->getResult(0), a);
        mapResultBlock(op->getResult(1), c);
        logIdx[op->getResult(0)] = logIdx.lookup(op->getOperand(0));
        logIdx[op->getResult(1)] = logIdx.lookup(op->getOperand(1));
        toErase.push_back(op);
      } else if (isa<SteaneXSyndromeOp>(op)) {
        Block data = blocks.lookup(op->getOperand(0));
        int ancIndex = ancIndices.lookup(op->getOperand(0));
        int logical = logIdx.lookup(op->getOperand(0));
        xSyndrome(b, loc, data, nextId, ancIndex, logical, stabRound);
        mapResultBlock(op->getResult(0), std::move(data));
        ancIndices[op->getResult(0)] = ancIndex; // Update the ancilla index for this logical qubit
        logIdx[op->getResult(0)] = logical;
        toErase.push_back(op);
      } else if (isa<SteaneZSyndromeOp>(op)) {
        Block data = blocks.lookup(op->getOperand(0));
        int ancIndex = ancIndices.lookup(op->getOperand(0));
        int logical = logIdx.lookup(op->getOperand(0));
        zSyndrome(b, loc, data, nextId, ancIndex, logical, stabRound);
        mapResultBlock(op->getResult(0), std::move(data));
        ancIndices[op->getResult(0)] = ancIndex; // Update the ancilla index for this logical qubit
        logIdx[op->getResult(0)] = logical;
        toErase.push_back(op);
      } else if (isa<ECRoundOp>(op)) {
        Block data = blocks.lookup(op->getOperand(0));
        int ancIndex = ancIndices.lookup(op->getOperand(0));
        int logical = logIdx.lookup(op->getOperand(0));
        xSyndrome(b, loc, data, nextId, ancIndex, logical, stabRound);
        zSyndrome(b, loc, data, nextId, ancIndex, logical, stabRound);
        mapResultBlock(op->getResult(0), std::move(data));
        ancIndices[op->getResult(0)] = ancIndex; // Update the ancilla index for this logical qubit
        logIdx[op->getResult(0)] = logical;
        toErase.push_back(op);
      } else if (auto meas = dyn_cast<LogicalMeasureOp>(op)) {
        Block data = blocks.lookup(op->getOperand(0));
        int logical = logIdx.lookup(op->getOperand(0));
        Value acc;
        for (int i = 0; i < STEANE_DATA; ++i) {
          Value bit = measure(b, loc, data[i], "m", "data", /*stab=*/-1,
                              /*round=*/-1, logical);
          acc = acc ? b.create<arith::XOrIOp>(loc, acc, bit).getResult()
                    : bit;
        }
        meas.getResult().replaceAllUsesWith(SmallVector<Value>{acc});
        toErase.push_back(op);
      } else {
        // Leave non-quantum ops (func.return, arith, resource_annotation) alone.
        // Any *unhandled ftqc op that touches quantum types* is a hard error.
        bool quantumFtqc = op->getDialect() &&
                           op->getDialect()->getNamespace() == "ftqc";
        auto isQ = [](Type t) {
          return isa<LogicalQubitType, LogicalRegType, MagicStateType,
                     SyndromeType, PauliFrameType>(t);
        };
        if (quantumFtqc) {
          bool touches = false;
          for (Value v : op->getOperands()) touches |= isQ(v.getType());
          for (Value v : op->getResults()) touches |= isQ(v.getType());
          if (touches) {
            op->emitError("ftqc-expand-physical: op '")
                << op->getName().getStringRef()
                << "' is not supported by the Steane physical expansion "
                   "(magic-state / non-Clifford / non-Steane ops are out of "
                   "scope for this scaffold)";
            failed = true;
            break;
          }
        }
      }
    }

    if (failed) {
      signalPassFailure();
      return;
    }

    // Erase originals in reverse program order (consumers before producers).
    for (Operation *op : llvm::reverse(toErase))
      op->erase();

    (void)ctx;
  }
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createExpandPhysicalPass() {
  return std::make_unique<FTQCExpandPhysicalPassImpl>();
}
