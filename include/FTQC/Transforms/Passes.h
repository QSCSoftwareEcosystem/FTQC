//===- Passes.h - FTQC Transform Pass Declarations ------------------------===//
//
// Part of the FTQC MLIR Project.
//
//===----------------------------------------------------------------------===//

/// @file Passes.h
/// @brief Factory functions and pipeline builders for all FTQC transformation
///        passes.
///
/// ### Pass overview
///
/// **Encoding passes** convert a logical circuit (already written in FTQC
/// logical-qubit types) to the chosen ECC representation:
///
/// | CLI flag | Factory | Description |
/// |---|---|---|
/// | `--ftqc-steane-encode` | createSteaneEncodePass() | Steane \[\[7,1,3\]\] encoding |
/// | `--ftqc-surface-encode` | createSurfaceEncodePass() | Surface code d=3..N |
/// | `--ftqc-color-encode` | createColorCodeEncodePass() | 2D triangular color code |
///
/// **Syndrome & correction passes** instrument the encoded circuit:
///
/// | CLI flag | Factory | Description |
/// |---|---|---|
/// | `--ftqc-insert-syndrome` | createInsertSyndromePass() | Insert syndrome extraction after each gate |
/// | `--ftqc-insert-correction` | createInsertCorrectionPass() | Insert `ftqc.correct` ops |
/// | `--ftqc-pauli-frame-opt` | createPauliFrameOptPass() | Defer corrections to a classical Pauli frame |
///
/// **Magic-state passes**:
///
/// | CLI flag | Factory | Description |
/// |---|---|---|
/// | `--ftqc-magic-distill` | createMagicDistillPass() | Replace `prepare_magic_t` with 15-to-1 / 7-to-1 distillation |
///
/// **Lowering passes**:
///
/// | CLI flag | Factory | Description |
/// |---|---|---|
/// | `--ftqc-transversal-gates` | createTransversalGatePass() | Verify / decompose logical gates into transversal form |
/// | `--ftqc-lower-to-quantum` | createLowerToQuantumPass() | Lower FTQC IR to a generic `quantum` dialect |
///
/// **Analysis passes** (read-only, annotate or print):
///
/// | CLI flag | Factory | Description |
/// |---|---|---|
/// | `--ftqc-resource-estimate` | createResourceEstimatePass() | Annotate functions with resource counts |
/// | `--ftqc-fault-path` | createFaultPathAnalysisPass() | Enumerate low-weight fault paths |
/// | `--ftqc-threshold` | createThresholdAnalysisPass() | Check physical error rate vs threshold |
///
/// ### Recommended pipeline for Steane code
/// @code{.cpp}
/// OpPassManager pm;
/// buildSteaneEncodePipeline(pm, /*distance=*/3, /*insertEC=*/true);
/// @endcode
/// which is equivalent to:
/// @code{.sh}
/// ftqc-opt --ftqc-insert-syndrome --ftqc-transversal-gates --ftqc-resource-estimate
/// @endcode

#ifndef FTQC_TRANSFORMS_PASSES_H
#define FTQC_TRANSFORMS_PASSES_H

#include "mlir/Pass/Pass.h"
#include <memory>
#include <string>

namespace mlir {
class ModuleOp;
namespace func { class FuncOp; }
namespace ftqc {

//===----------------------------------------------------------------------===//
// Encoding pass factories
//===----------------------------------------------------------------------===//

/// @brief Create the Steane \[\[7,1,3\]\] encoding pass with default options.
std::unique_ptr<mlir::Pass> createSteaneEncodePass();

/// @brief Create the Steane encoding pass with explicit options.
/// @param distance Code distance (must be 3 for [[7,1,3]]; reserved for future
///                 concatenation).
/// @param insertEC If true, insert syndrome and correction rounds during
///                 encoding.
/// @param ecRounds Number of syndrome rounds to insert per gate.
std::unique_ptr<mlir::Pass> createSteaneEncodePass(unsigned distance,
                                                    bool insertEC,
                                                    unsigned ecRounds);

/// @brief Create the surface-code encoding pass with default options (d=3,
///        lattice surgery enabled).
std::unique_ptr<mlir::Pass> createSurfaceEncodePass();

/// @brief Create the surface-code encoding pass with explicit options.
/// @param distance Code distance d; physical qubit count = 2d²-1.
/// @param useLattice If true, implement two-qubit gates via lattice surgery;
///                   otherwise use a transversal approximation.
std::unique_ptr<mlir::Pass> createSurfaceEncodePass(unsigned distance,
                                                     bool useLattice);

/// @brief Create the 2D triangular color-code encoding pass (stub).
/// @note Full lowering is not yet implemented; the pass emits a remark.
std::unique_ptr<mlir::Pass> createColorCodeEncodePass();

/// @brief Create the color-code encoding pass with explicit distance.
/// @param distance Code distance (typically 3, 5, or 7).
std::unique_ptr<mlir::Pass> createColorCodeEncodePass(unsigned distance);

//===----------------------------------------------------------------------===//
// Syndrome and correction pass factories
//===----------------------------------------------------------------------===//

/// @brief Create the syndrome-insertion pass with default options
///        (`method="steane"`, `rounds=3`).
std::unique_ptr<mlir::Pass> createInsertSyndromePass();

/// @brief Create the syndrome-insertion pass with explicit options.
///
/// Inserts syndrome extraction operations after every logical gate in each
/// `func.func` in the module.
///
/// @param method Syndrome extraction method: `"steane"` (transversal ancilla),
///               `"shor"` (cat-state), or `"flag"` (Chao-Reichardt flagging).
/// @param rounds Number of consecutive syndrome rounds per gate.
std::unique_ptr<mlir::Pass> createInsertSyndromePass(const std::string &method,
                                                     unsigned rounds);

/// @brief Create the correction-insertion pass.
///
/// Inserts `ftqc.correct` operations after each syndrome block, consuming the
/// syndrome results and emitting corrections on the logical qubit.
std::unique_ptr<mlir::Pass> createInsertCorrectionPass();

/// @brief Create the Pauli-frame optimisation pass.
///
/// Converts physical correction operations into classical updates to a Pauli
/// frame, eliminating redundant mid-circuit physical gates and deferring all
/// corrections to the final measurement.
std::unique_ptr<mlir::Pass> createPauliFrameOptPass();

//===----------------------------------------------------------------------===//
// Magic state pass factories
//===----------------------------------------------------------------------===//

/// @brief Create the magic-state distillation pass with default options
///        (`protocol="15to1"`, `levels=1`, `targetError=1e-10`).
std::unique_ptr<mlir::Pass> createMagicDistillPass();

/// @brief Create the magic-state distillation pass with explicit options.
///
/// Replaces every `ftqc.prepare_magic_t` with an inline distillation
/// subroutine.  Multiple levels can be chained to achieve sub-threshold
/// output error rates.
///
/// @param protocol Distillation protocol: `"15to1"` (Bravyi-Kitaev, p_out ≈
///                 35 p_in³) or `"7to1"` (Steane-inner, p_out ≈ 7 p_in³).
/// @param levels   Number of distillation levels to chain.
/// @param targetError  Target output error rate; the pass will warn if
///                 unachievable with the given @p levels.
std::unique_ptr<mlir::Pass> createMagicDistillPass(const std::string &protocol,
                                                    unsigned levels,
                                                    double targetError);

//===----------------------------------------------------------------------===//
// Lowering pass factories
//===----------------------------------------------------------------------===//

/// @brief Create the lowering pass to a generic `quantum` dialect.
std::unique_ptr<mlir::Pass> createLowerToQuantumPass();

/// @brief Create the STIM physical circuit translation pass with default options
///        (`output-file=""` writes to stderr, `physical-error-rate=1e-3`,
///         `syndrome-rounds=1`, `include-comments=true`).
///
/// Expands each logical FTQC qubit into a Steane [[7,1,3]] physical block and
/// emits a STIM stabilizer circuit for QEC Monte Carlo simulation.  The STIM
/// string is also attached as the `ftqc.stim` module attribute.
std::unique_ptr<mlir::Pass> createLowerToSTIMPass();

/// @brief Create the QIR Base Profile translation pass with default options
///        (`output-file=""` writes to stderr, `include-comments=true`).
///
/// The pass walks every `func.func` in the module and emits a QIR Base Profile
/// LLVM IR string mapping logical gates to `__quantum__qis__*` runtime calls.
/// Qubit and result indices follow the Base Profile opaque-pointer convention.
/// All measurement results are recorded via `__quantum__rt__result_record_output`
/// at function exit.  The IR is conformant with QIR Base Profile v1.0.
std::unique_ptr<mlir::Pass> createLowerToQIRPass();

/// @brief Create the QASM 3.0 translation pass with default options
///        (`output-file=""` writes to stderr, `include-comments=true`).
///
/// The pass walks every `func.func` in the module and emits an OpenQASM 3.0
/// string mapping logical gates to standard QASM gates (h, x, y, z, s, t,
/// cx, cz, ccx, measure).  FT-infrastructure ops (syndrome extraction,
/// error correction, Pauli-frame updates) emit QASM line comments.
///
/// The QASM string is also attached to the module as the `ftqc.qasm3`
/// attribute so that FileCheck tests can verify it from the MLIR IR text.
std::unique_ptr<mlir::Pass> createLowerToQASM3Pass();

/// Options are set on the command line:
///   --ftqc-lower-to-qasm3="output-file=/path/to/out.qasm include-comments=false"

/// @brief Create the transversal-gate pass with default options
///        (non-transversal gates emit a warning but are not decomposed).
std::unique_ptr<mlir::Pass> createTransversalGatePass();

/// @brief Create the transversal-gate pass with explicit decomposition option.
/// @param decomposeNonTransversal If true, non-transversal gates (e.g. T on a
///        surface code) are automatically replaced with magic-state injection.
///        If false, a warning is emitted but the op is left in place.
std::unique_ptr<mlir::Pass> createTransversalGatePass(bool decomposeNonTransversal);

//===----------------------------------------------------------------------===//
// Analysis pass factories
//===----------------------------------------------------------------------===//

/// @brief Create the resource-estimation pass (does not print a report).
std::unique_ptr<mlir::Pass> createResourceEstimatePass();

/// @brief Create the resource-estimation pass.
/// @param printReport If true, prints a human-readable resource summary to
///                    stderr at the end of the pass.
std::unique_ptr<mlir::Pass> createResourceEstimatePass(bool printReport);

/// @brief Create the fault-path analysis pass with default max order 1.
std::unique_ptr<mlir::Pass> createFaultPathAnalysisPass();

/// @brief Create the fault-path analysis pass.
/// @param maxOrder Maximum weight of fault paths to enumerate.  Higher values
///                 give more complete coverage at exponential runtime cost.
std::unique_ptr<mlir::Pass> createFaultPathAnalysisPass(unsigned maxOrder);

/// @brief Create the threshold-analysis pass with default parameters
///        (`physicalError=1e-3`, `targetError=1e-10`).
std::unique_ptr<mlir::Pass> createThresholdAnalysisPass();

/// @brief Create the threshold-analysis pass with explicit parameters.
/// @param physicalError Physical gate error rate (probability per gate).
/// @param targetError   Target logical error rate per logical gate.
std::unique_ptr<mlir::Pass> createThresholdAnalysisPass(double physicalError,
                                                         double targetError);

//===----------------------------------------------------------------------===//
// Pre-built pipelines
//===----------------------------------------------------------------------===//

/// @brief Build the full Steane \[\[7,1,3\]\] fault-tolerant lowering pipeline.
///
/// Pipeline (in order):
///  1. `ftqc-steane-encode` — encode at distance @p distance
///  2. `ftqc-insert-syndrome` (if @p insertEC) — @p syndromeMethod rounds
///  3. `ftqc-insert-correction` (if @p insertEC)
///  4. `ftqc-transversal-gates`
///  5. `ftqc-resource-estimate`
///
/// @param pm             Pass manager to append passes to.
/// @param distance       Steane code distance (default 3).
/// @param insertEC       Insert syndrome extraction and correction (default true).
/// @param syndromeMethod Syndrome method passed to createInsertSyndromePass()
///                       (default `"steane"`).
void buildSteaneEncodePipeline(mlir::OpPassManager &pm,
                               unsigned distance = 3,
                               bool insertEC = true,
                               const std::string &syndromeMethod = "steane");

/// @brief Build the full surface-code fault-tolerant lowering pipeline.
///
/// Pipeline: encode → insert-syndrome (steane method) → insert-correction →
/// resource-estimate.
///
/// @param pm          Pass manager to append passes to.
/// @param distance    Surface-code distance d (default 3).
/// @param useLattice  Use lattice surgery for two-qubit gates (default true).
void buildSurfaceEncodePipeline(mlir::OpPassManager &pm,
                                unsigned distance = 3,
                                bool useLattice = true);

/// @brief Build the T-gate magic-state distillation pipeline.
///
/// Appends `ftqc-magic-distill` configured for the given protocol and level
/// count.  Use after an encoding pipeline to handle T gates.
///
/// @param pm       Pass manager to append passes to.
/// @param protocol Distillation protocol (`"15to1"` or `"7to1"`).
/// @param levels   Number of distillation levels (default 1).
void buildDistillationPipeline(mlir::OpPassManager &pm,
                               const std::string &protocol = "15to1",
                               unsigned levels = 1);

/// @brief Register all FTQC passes with the global MLIR pass registry.
///
/// Must be called before using any FTQC pass by name from the command line or
/// `PassManager::parsePassPipeline()`.
void registerFTQCPasses();

} // namespace ftqc
} // namespace mlir

// Include TableGen-generated pass declarations.
#define GEN_PASS_DECL
#include "FTQC/Transforms/Passes.h.inc"

#endif // FTQC_TRANSFORMS_PASSES_H
