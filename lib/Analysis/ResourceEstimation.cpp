//===- ResourceEstimation.cpp - FTQC resource counting --------------------===//
#include "FTQC/Analysis/ResourceEstimation.h"
#include "FTQC/Dialect/FTQCAttrs.h"
#include "FTQC/Dialect/FTQCOps.h"
#include "FTQC/Dialect/FTQCTypes.h"
#include "FTQC/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>
#include <sstream>

using namespace mlir;
using namespace mlir::ftqc;

//===----------------------------------------------------------------------===//
// ECC physical parameter tables
//===----------------------------------------------------------------------===//

namespace {

/// Physical qubit parameters derived from ECC kind and code distance.
struct ECCPhysicalParams {
  unsigned physPerLogical;    ///< Data qubits per logical qubit.
  unsigned ancillaPerLogical; ///< Ancilla qubits per logical qubit (reused each round).
  double   threshold;         ///< Fault-tolerance threshold p_th.
};

/// Return physical qubit parameters for the given ECC family and distance.
///
/// Formulas:
///   Steane [[7,1,3]]:
///     data = 7  (fixed; Hamming code structure)
///     ancilla = 6  (3 X-stabiliser + 3 Z-stabiliser measurement qubits)
///     threshold ≈ 1 %
///
///   Rotated surface code, distance d:
///     data    = d²          (physical data qubits on the d×d lattice)
///     ancilla = d² − 1      (X + Z check qubits, reused each round)
///     threshold ≈ 1 %
///
///   Triangular 2D color code, distance d:
///     data    = (3d² + 1) / 4   (approximately half the surface overhead)
///     ancilla = d²  / 2
///     threshold ≈ 0.3 %  (more noise-sensitive than surface code)
ECCPhysicalParams eccPhysicalParams(ECCKind kind, unsigned d) {
  switch (kind) {
  case ECCKind::Steane:
    return {7u, 6u, 0.01};

  case ECCKind::Surface:
    return {d * d, d * d > 0 ? d * d - 1 : 0, 0.01};

  case ECCKind::ColorCode:
    return {(3u * d * d + 1u) / 4u, d * d / 2u, 0.003};

  case ECCKind::Concatenated:
    // Concatenated code level-k: each level squares the suppression.
    // Approx: n = 7^k, ancilla = 6^k  (Steane base code, k = distance param)
    // Use 'd' as concatenation level here.
    return {1u << (3u * d), (1u << (3u * d)) - 1u, 0.01};

  case ECCKind::Repetition:
    // Classical repetition code on d qubits: only corrects bit-flip errors.
    return {d, d - 1u, 0.5}; // threshold 50% (only bit-flip protection)

  case ECCKind::Compass:
    // Compass codes interpolate between repetition and surface code.
    // Approximate as a sub-optimal surface code: d² data, (d²-1)/2 ancilla.
    return {d * d, (d * d - 1u) / 2u, 0.005};
  }
  return {7u, 6u, 0.01}; // unreachable
}

/// Human-readable string for an ECC family.
llvm::StringRef eccName(ECCKind kind) {
  switch (kind) {
  case ECCKind::Steane:       return "Steane [[7,1,3]]";
  case ECCKind::Surface:      return "surface code";
  case ECCKind::ColorCode:    return "2D color code";
  case ECCKind::Concatenated: return "concatenated code";
  case ECCKind::Repetition:   return "repetition code";
  case ECCKind::Compass:      return "compass code";
  }
  return "unknown";
}

} // anonymous namespace

//===----------------------------------------------------------------------===//
// ResourceReport::toString
//===----------------------------------------------------------------------===//

std::string ResourceReport::toString() const {
  std::ostringstream ss;
  ss << "=== FTQC Resource Report ===\n";

  // ── ECC metadata ──────────────────────────────────────────────────────────
  if (eccDetected) {
    ss << "ECC code              : " << eccName(eccKind).str()
       << "  d=" << eccDistance << "\n";
  } else {
    ss << "ECC code              : (not detected — no logical qubits found)\n";
  }

  // ── Logical resources ─────────────────────────────────────────────────────
  ss << "Logical qubits        : " << logicalQubits << "\n";
  ss << "Logical T gates       : " << logicalTGates  << "\n";
  ss << "Logical CNOT gates    : " << logicalCnotGates << "\n";
  ss << "Logical Clifford gates: " << logicalCliffordGates << "\n";
  ss << "EC rounds             : " << ecRounds << "\n";
  ss << "Distillation rounds   : " << distillationRounds << "\n";

  // ── Physical resources ────────────────────────────────────────────────────
  ss << "---\n";
  ss << "Physical data qubits  : " << physicalDataQubits << "\n";
  ss << "Physical ancilla      : " << physicalAncillaQubits << "\n";
  ss << "Physical total        : " << physicalTotalQubits << "\n";
  ss << "Distillation qubits   : " << distillationQubits << "\n";

  // ── Error estimates ───────────────────────────────────────────────────────
  ss << "---\n";
  ss << "Physical error rate   : " << physicalErrorRate << "\n";
  ss << "Est. logical error    : ";
  if (physicalErrorRate == 0.0) {
    ss << "(set --phys-error to compute)\n";
  } else {
    // Format as scientific notation for small values
    std::ostringstream val;
    val << std::scientific << estimatedLogicalError;
    ss << val.str() << "\n";
  }
  return ss.str();
}

//===----------------------------------------------------------------------===//
// ResourceReport::computePhysical
//===----------------------------------------------------------------------===//

void ResourceReport::computePhysical(unsigned physPerLogical,
                                      unsigned ancillaPerLogical,
                                      double physError,
                                      double threshold,
                                      unsigned distance) {
  physicalErrorRate      = physError;
  physicalDataQubits     = logicalQubits * physPerLogical;
  physicalAncillaQubits  = logicalQubits * ancillaPerLogical;
  physicalTotalQubits    = physicalDataQubits + physicalAncillaQubits
                         + distillationQubits;

  // Leading-order logical error rate:
  //   p_L ≈ 0.1 × (p / p_th)^⌈(d+1)/2⌉   per syndrome round per logical qubit
  //   p_total = p_L × ecRounds × logicalQubits
  //
  // Above threshold: errors accumulate faster than they are corrected → p_L = 1.
  if (threshold > 0.0 && physError < threshold) {
    unsigned t = (distance + 1u) / 2u;          // exponent ⌈(d+1)/2⌉
    double pL  = 0.1 * std::pow(physError / threshold, static_cast<double>(t));
    estimatedLogicalError = pL * static_cast<double>(ecRounds)
                               * static_cast<double>(logicalQubits);
    if (estimatedLogicalError > 1.0)
      estimatedLogicalError = 1.0;
  } else {
    estimatedLogicalError = 1.0; // above threshold: uncorrectable
  }
}

//===----------------------------------------------------------------------===//
// ResourceEstimator
//===----------------------------------------------------------------------===//

/// Post-walk fixup: if no explicit Distill15to1Op / Distill7to1Op were seen
/// (i.e. --ftqc-magic-distill has NOT yet run), infer distillation overhead
/// from the logical T gate count (one 15-to-1 round per T gate).
static void inferDistillationIfAbsent(ResourceReport &r) {
  if (r.distillationRounds == 0 && r.logicalTGates > 0) {
    r.distillationRounds = r.logicalTGates;
    r.distillationQubits = std::max(r.distillationQubits, (uint64_t)105);
  }
}

ResourceReport ResourceEstimator::estimate(func::FuncOp func) {
  report_ = ResourceReport{};
  func.walk([this](Operation *op) { visitOp(op); });
  inferDistillationIfAbsent(report_);
  return report_;
}

ResourceReport ResourceEstimator::estimateModule(ModuleOp mod) {
  report_ = ResourceReport{};
  mod.walk([this](Operation *op) { visitOp(op); });
  inferDistillationIfAbsent(report_);
  return report_;
}

/// Try to detect ECC info from a LogicalQubitType; store in report_ if found.
void ResourceEstimator::tryDetectECC(mlir::Type t) {
  if (report_.eccDetected)
    return;
  if (auto qt = mlir::dyn_cast<LogicalQubitType>(t)) {
    report_.eccKind     = qt.getCode().getKind();
    report_.eccDistance = qt.getCode().getDistance();
    report_.eccDetected = true;
  }
}

void ResourceEstimator::visitOp(Operation *op) {
  llvm::TypeSwitch<Operation*, void>(op)
    // Scan function argument types — handles circuits like QFT-3 where
    // logical qubits arrive as function arguments rather than init_zero ops.
    .Case<func::FuncOp>([&](func::FuncOp func) {
      for (auto argType : func.getFunctionType().getInputs()) {
        if (auto qt = mlir::dyn_cast<LogicalQubitType>(argType)) {
          ++report_.logicalQubits;
          tryDetectECC(argType);
        }
      }
    })
    .Case<InitZeroOp, InitPlusOp>([&](Operation *initOp) {
      ++report_.logicalQubits;
      tryDetectECC(initOp->getResult(0).getType());
    })
    .Case<EncodeOp>([&](auto) {
      ++report_.logicalQubits;
    })
    .Case<LogicalTOp>([&](auto) {
      ++report_.logicalTGates;
      // Distillation rounds are counted from explicit Distill15to1Op /
      // Distill7to1Op when --ftqc-magic-distill has already run.
      // If those ops are absent we infer the cost post-walk (see below).
    })
    .Case<LogicalCNOTOp, LatticeSurgeryOp>([&](auto) {
      ++report_.logicalCnotGates;
    })
    .Case<LogicalHOp, LogicalXOp, LogicalZOp, LogicalSOp, LogicalYOp,
          LogicalCZOp>([&](auto) {
      ++report_.logicalCliffordGates;
    })
    .Case<ExtractSyndromeOp, SteaneXSyndromeOp, SteaneZSyndromeOp,
          ShorSyndromeOp, FlagSyndromeOp, SurfaceRoundOp>([&](auto) {
      ++report_.ecRounds;
    })
    .Case<Distill15to1Op>([&](auto) {
      // Explicit distillation op inserted by --ftqc-magic-distill.
      report_.distillationRounds += 1;
      report_.distillationQubits =
          std::max(report_.distillationQubits, (uint64_t)105);
    })
    .Case<Distill7to1Op>([&](auto) {
      report_.distillationRounds += 1;
      report_.distillationQubits =
          std::max(report_.distillationQubits, (uint64_t)49); // 7 × 7
    })
    .Case<LogicalMeasureOp>([&](auto) {
      ++report_.logicalMeasurements;
    })
    .Default([](auto) {});
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

namespace {

struct FTQCResourceEstimatePassImpl
    : public PassWrapper<FTQCResourceEstimatePassImpl,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(FTQCResourceEstimatePassImpl)

  FTQCResourceEstimatePassImpl() = default;
  explicit FTQCResourceEstimatePassImpl(bool pr) : printReport_(pr) {}

  StringRef getArgument() const override { return "ftqc-resource-estimate"; }

  void runOnOperation() override {
    auto mod = getOperation();
    ResourceEstimator est;
    auto report = est.estimateModule(mod);

    // Derive physical qubit counts and logical error rate from the ECC
    // detected in the IR.  physError = 1e-3 is a typical superconducting
    // qubit gate error rate; adjust via future pass option if needed.
    if (report.eccDetected) {
      auto params = eccPhysicalParams(report.eccKind, report.eccDistance);
      constexpr double physError = 1e-3;
      report.computePhysical(params.physPerLogical, params.ancillaPerLogical,
                             physError, params.threshold, report.eccDistance);
    }

    // Annotate each function with the computed totals.
    mod.walk([&](func::FuncOp func) {
      OpBuilder builder(func.getBody().front().getTerminator());
      builder.setInsertionPointToStart(&func.getBody().front());
      builder.create<ResourceAnnotationOp>(
          func.getLoc(),
          builder.getI64IntegerAttr(report.physicalTotalQubits),
          builder.getI64IntegerAttr(report.logicalQubits),
          builder.getI64IntegerAttr(report.logicalTGates),
          builder.getI64IntegerAttr(report.logicalCnotGates),
          builder.getI64IntegerAttr(report.ecRounds),
          builder.getF64FloatAttr(report.estimatedLogicalError));
    });

    if (printReport_)
      llvm::errs() << report.toString();
  }

  bool printReport_ = true;
};

} // namespace

std::unique_ptr<mlir::Pass> mlir::ftqc::createResourceEstimatePass() {
  return std::make_unique<FTQCResourceEstimatePassImpl>();
}
std::unique_ptr<mlir::Pass> mlir::ftqc::createResourceEstimatePass(bool pr) {
  return std::make_unique<FTQCResourceEstimatePassImpl>(pr);
}
