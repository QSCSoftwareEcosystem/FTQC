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

/// @brief Create the Intel-QS C++ source emitter pass.
///
/// Emits a standalone, compilable C++ source file that simulates the logical
/// FTQC circuit using the Intel-QS QubitRegister<ComplexDP> API (or
/// NoisyQureg<ComplexDP> with the --with-noise option).
///
/// The emitted file can be compiled directly against the Intel-QS library:
///   g++ -std=c++17 -I${IQS_ROOT}/include out.cpp \
///       -L${IQS_ROOT}/build/lib -lintelqs_lib -lm -o sim && ./sim [shots]
std::unique_ptr<mlir::Pass> createLowerToIntelQSCppPass();

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

/// @brief Create the QIR Base Profile translation pass with no ECC expansion
///        and QASM-like 1:1 logical lowering semantics.
///
/// This pass emits one QIR qubit per logical qubit (no Steane/surface/color
/// physical block expansion), mapping each logical gate to exactly one QIS
/// call.  It is useful for direct algorithm-level IR export comparable to
/// `ftqc-lower-to-qasm3`, while still targeting QIR.
std::unique_ptr<mlir::Pass> createLowerToQIRnsPass();

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

/// @brief Create the IQM JSON translation pass with default options
///        (`output-file=""` writes to stderr, `include-comments=true`,
///         `expand-syndromes=true`).
///
/// The pass walks every `func.func` in the module and emits an IQM JSON
/// circuit string, decomposing logical gates into IQM native gates (prx, cz)
/// and measure operations.  Syndrome ops are optionally expanded into
/// physical ancilla circuits using CZ gates.
///
/// The JSON string is also attached to the module as the `ftqc.iqm_json`
/// attribute so that FileCheck tests can verify it from the MLIR IR text.
std::unique_ptr<mlir::Pass> createLowerToIQMJSONPass();
/// Variant selecting prx angle units: emitRadians=true emits angle/phase in
/// radians (value*2pi); false (default) emits angle_t/phase_t in turns.
std::unique_ptr<mlir::Pass> createLowerToIQMJSONPass(bool emitRadians);
/// Variant also selecting physical expansion: physical=true expands each Steane
/// logical qubit into a 7-physical-qubit block (encoder + transversal gates).
std::unique_ptr<mlir::Pass> createLowerToIQMJSONPass(bool emitRadians, bool physical);

/// Options are set on the command line:
///   --ftqc-lower-to-qasm3="output-file=/path/to/out.qasm include-comments=false"

/// @brief Create the lattice-surgery surface-code STIM translation pass.
///
/// Expands each logical qubit into a rotated d=3 surface-code block (17 qubits)
/// and emits a STIM circuit.  Two-qubit gates use lattice surgery joint
/// measurements (MPP instruction).  Single-qubit Clifford gates are transversal.
std::unique_ptr<mlir::Pass> createLowerToLatticeSurgeryPass();

/// @brief Create the transversal-gate pass with default options
///        (non-transversal gates emit a warning but are not decomposed).
std::unique_ptr<mlir::Pass> createTransversalGatePass();

/// @brief Create the transversal-gate pass with explicit decomposition option.
/// @param decomposeNonTransversal If true, non-transversal gates (e.g. T on a
///        surface code) are automatically replaced with magic-state injection.
///        If false, a warning is emitted but the op is left in place.
std::unique_ptr<mlir::Pass> createTransversalGatePass(bool decomposeNonTransversal);

/// @brief Create the multi-controlled-X decomposition pass
///        (`--ftqc-decompose-mcx`): rewrites each `ftqc.logical_mcx` into a
///        clean-ancilla `logical_toffoli` V-chain (X/CNOT/Toffoli for 0/1/2
///        controls).
std::unique_ptr<mlir::Pass> createDecomposeMCXPass();

/// @brief Create the Rz-synthesis pass (`--ftqc-synthesize-rz`): approximates
///        each `ftqc.logical_rz` by a Clifford+T sequence (Solovay-Kitaev) to a
///        target precision and reports the T-count.
std::unique_ptr<mlir::Pass> createSynthesizeRzPass();
std::unique_ptr<mlir::Pass> createSynthesizeRzPass(double epsilon);

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
// Physical expansion (qphys layer)
//===----------------------------------------------------------------------===//

/// @brief Create the physical-expansion pass (`--ftqc-expand-physical`).
///
/// Unfolds each `!ftqc.logical_qubit` into its physical block of
/// `!ftqc.qphys_qubit` values and rewrites logical gates / syndrome ops into
/// `ftqc.qphys.*` physical gates.  Data and ancilla qubits become explicit,
/// labeled SSA values.  Currently expands the Steane [[7,1,3]] Clifford +
/// syndrome + measurement subset; unhandled logical ops (T / magic-state /
/// distillation / surface / color) cause a pass error rather than broken IR.
std::unique_ptr<mlir::Pass> createExpandPhysicalPass();

/// @brief Create the qphys → OpenQASM 3.0 lowering (`--ftqc-qphys-to-qasm3`).
///
/// Demonstrates lowering a *physical* IR (post `--ftqc-expand-physical`) to a
/// back end: walks `ftqc.qphys.*` ops and emits physical-qubit QASM, proving
/// that emitters can consume one shared physical IR instead of re-deriving the
/// physical layout per back end.
std::unique_ptr<mlir::Pass> createQPhysToQASM3Pass();

/// @brief Create the qphys → IQM JSON lowering (`--ftqc-qphys-to-iqm-json`).
///
/// Retargets the IQM back end onto the shared physical IR: each qphys qubit
/// maps to hardware locus "QB<id+1>" and physical gates are decomposed into
/// IQM native gates {prx, cz, measure}.  Replaces the need for the logical
/// LowerToIQMJSON emitter to re-derive the physical (data+ancilla) layout.
std::unique_ptr<mlir::Pass> createQPhysToIQMJSONPass();

/** Create the physical-qubit initial-layout pass. With no input file, it is a no-op. */
std::unique_ptr<mlir::Pass> createQPhysInitLayoutPass();

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
