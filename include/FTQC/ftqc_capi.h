//===- ftqc_capi.h - IRIS kernel C-API for ftqc-opt ------------------===//
//
// C-API library wrapping ftqc-opt pass infrastructure for use as IRIS task
// kernels in classical-quantum workflows.
//
// Two entry points:
//   ftqc_mlir_opt  - MLIR input in memory + FTQCCfg configuration
//   ftqc_qasm_opt  - OpenQASM 2/3 input in memory + FTQCCfg configuration
//
// Both functions receive input as a raw memory pointer (no file I/O) and write
// output to a newly allocated buffer that the caller must release with
// ftqc_free().
//
// Configuration (FTQCCfg)
// ────────────────────────
// All fields use fixed-size storage (no pointers) so the struct can be
// byte-copied into IRIS device memory without pointer-validity concerns.
//
// Zero-initialise the struct and set only the fields you need; zero / empty
// fields fall back to documented defaults.
//
// Return value: 0 on success, non-zero on failure.
//
//===----------------------------------------------------------------------===//

#ifndef FTQC_IRIS_CAPI_H
#define FTQC_IRIS_CAPI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configuration passed to ftqc_mlir_opt / ftqc_qasm_opt.
 *
 * All string fields are fixed-size char arrays (no pointers).  Zero-initialise
 * with memset/calloc and set only the fields you need.
 */
typedef struct FTQCCfg {
    /* ── Input / import ──────────────────────────────────────────────────── */
    /** ECC scheme: "steane" | "surface" | "color_code"   (default "steane") */
    char ecc_kind[64];
    /** Code distance, positive integer                    (default 3)        */
    int  ecc_dist;
    /** MLIR function name for QASM import                 (default "circuit") */
    char func_name[64];

    /* ── Pre-built pipelines (checked first; mutually exclusive) ─────────── */
    /** Run the full Steane [[7,1,3]] encode + EC pipeline */
    int run_steane_pipeline;
    /** Run the full surface-code encode + EC pipeline */
    int run_surface_pipeline;
    /** Run the T-gate magic-state distillation pipeline */
    int run_distill_pipeline;

    /* ── Encoding passes ─────────────────────────────────────────────────── */
    /** --ftqc-steane-encode  : Steane [[7,1,3]] encoding */
    int steane_encode;
    /** --ftqc-surface-encode : surface-code encoding */
    int surface_encode;
    /** surface: use lattice surgery for two-qubit gates   (default 1) */
    int use_lattice;
    /** --ftqc-color-encode   : 2D triangular color-code encoding */
    int color_encode;

    /* ── Syndrome & correction passes ────────────────────────────────────── */
    /** --ftqc-insert-syndrome : insert syndrome extraction rounds */
    int  insert_syndrome;
    /** Syndrome method: "steane" | "shor" | "flag"        (default "steane") */
    char syndrome_method[32];
    /** Number of syndrome rounds per gate                  (default 3) */
    int  syndrome_rounds;
    /** --ftqc-insert-correction : insert ftqc.correct ops */
    int  insert_correction;
    /** --ftqc-pauli-frame-opt   : defer corrections to a classical Pauli frame */
    int  pauli_frame_opt;

    /* ── Magic-state passes ──────────────────────────────────────────────── */
    /** --ftqc-magic-distill : replace prepare_magic_t with distillation */
    int    magic_distill;
    /** Distillation protocol: "15to1" | "7to1"            (default "15to1") */
    char   distill_protocol[32];
    /** Number of distillation levels to chain              (default 1) */
    int    distill_levels;
    /** Target output error rate                            (default 1e-10) */
    double distill_target_error;

    /* ── Lowering passes ─────────────────────────────────────────────────── */
    /** --ftqc-transversal-gates : verify/decompose into transversal form */
    int transversal_gates;
    /** Decompose non-transversal gates via magic-state injection */
    int decompose_non_transversal;
    /** --ftqc-lower-to-quantum        : lower to generic quantum dialect */
    int lower_to_quantum;
    /** --ftqc-lower-to-stim           : emit STIM stabilizer circuit */
    int lower_to_stim;
    /** --ftqc-lower-to-qir            : emit QIR Base Profile LLVM IR */
    int lower_to_qir;
    /** --ftqc-lower-to-qasm3          : emit OpenQASM 3.0 */
    int lower_to_qasm3;
    /** --ftqc-lower-to-iqm-json       : emit IQM JSON circuit */
    int lower_to_iqm_json;
    /** --ftqc-lower-to-lattice-surgery: lattice-surgery surface-code STIM */
    int lower_to_lattice_surgery;
    /** --ftqc-lower-to-iqs : lower to Intel-QS __iqs__* runtime calls */
    int lower_to_iqs;

    /* ── Analysis passes ─────────────────────────────────────────────────── */
    /** --ftqc-resource-estimate : annotate functions with resource counts */
    int    resource_estimate;
    /** Print resource summary to stderr after the pass */
    int    print_report;
    /** --ftqc-fault-path : enumerate low-weight fault paths */
    int    fault_path;
    /** Maximum fault-path weight to enumerate              (default 1) */
    int    fault_path_max_order;
    /** --ftqc-threshold : check physical error rate vs threshold */
    int    threshold_analysis;
    /** Physical gate error rate                            (default 1e-3) */
    double physical_error_rate;
    /** Target logical error rate                           (default 1e-10) */
    double target_error_rate;

    /** When lower_to_iqm_json: emit prx as angle/phase in radians (value*2pi)
     *  instead of angle_t/phase_t in turns. Required by modern IQM
     *  station-control. (Appended at end to keep struct layout stable.) */
    int lower_to_iqm_json_radians;
} FTQCCfg;

/**
 * Run the ftqc-opt pass pipeline on MLIR text held in memory.
 *
 * @param mlir_data  Pointer to MLIR text; need not be NUL-terminated.
 * @param mlir_size  Byte length of mlir_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_mlir_opt(const void    *mlir_data,
                  size_t         mlir_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size);

/**
 * Run the ftqc-opt pass pipeline on MLIR text held in memory (IRIS kernel).
 *
 * Unlike ftqc_mlir_opt, the output is written directly into a caller-supplied
 * buffer (the IRIS device buffer for the flush_ numpy array), so no malloc
 * occurs and no ftqc_free() call is needed.
 *
 * @param spec       Pointer to IrisSpec (ignored; present for IRIS FFI ABI).
 * @param mlir_data  Pointer to MLIR text; need not be NUL-terminated.
 * @param mlir_size  Byte length of mlir_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 * @param out_buf    Pre-allocated output buffer; output MLIR text is written
 *                   here directly (must be large enough).
 * @param out_size   On success, receives the byte length of the output written
 *                   to out_buf.
 * @return           0 on success, non-zero on failure.
 */
int ftqc_iris_mlir_opt(const void    *spec,
                       const void    *mlir_data,
                       size_t         mlir_size,
                       const FTQCCfg *cfg,
                       void          *out_buf,
                       size_t        *out_size);

/**
 * Import an OpenQASM 2/3 circuit from memory, then run the ftqc-opt pass
 * pipeline on the resulting MLIR.
 *
 * @param qasm_data  Pointer to QASM source text; need not be NUL-terminated.
 * @param qasm_size  Byte length of qasm_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_qasm_opt(const void    *qasm_data,
                  size_t         qasm_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
                  size_t        *out_size);

/**
 * Import an OpenQASM 2/3 circuit from memory, then run the ftqc-opt pass
 * pipeline on the resulting MLIR (IRIS kernel).
 *
 * Unlike ftqc_qasm_opt, the output is written directly into a caller-supplied
 * buffer (the IRIS device buffer for the flush_ numpy array), so no malloc
 * occurs and no ftqc_free() call is needed.
 *
 * @param spec       Pointer to IrisSpec (ignored; present for IRIS FFI ABI).
 * @param qasm_data  Pointer to QASM source text; need not be NUL-terminated.
 * @param qasm_size  Byte length of qasm_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 * @param out_buf    Pre-allocated output buffer; output MLIR text is written
 *                   here directly (must be large enough).
 * @param out_size   On success, receives the byte length of the output written
 *                   to out_buf.
 * @return           0 on success, non-zero on failure.
 */
int ftqc_iris_qasm_opt(const void    *spec,
                       const void    *qasm_data,
                       size_t         qasm_size,
                       const FTQCCfg *cfg,
                       void          *out_buf,
                       size_t        *out_size);

/**
 * Release a buffer returned by ftqc_mlir_opt or ftqc_qasm_opt.
 * Passing NULL is safe and has no effect.
 */
void ftqc_free(void *data);



#ifdef __cplusplus
}
#endif

#endif /* FTQC_IRIS_CAPI_H */
