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
// Pass NULL for cfg to use all defaults.  Individual NULL / zero fields also
// fall back to their defaults:
//
//   ecc_kind   — "steane" | "surface" | "color_code"    (default: "steane")
//   ecc_dist   — code distance, positive integer         (default: 3)
//   func_name  — MLIR function name for QASM import      (default: "circuit")
//   pipeline   — comma-separated pass/pipeline names     (default: "")
//                e.g. "ftqc-insert-syndrome,ftqc-resource-estimate"
//                or   "ftqc-steane-pipeline"
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
 * Zero-initialise and set only the fields you need; NULL / 0 values fall back
 * to their documented defaults.
 */
typedef struct FTQCCfg {
    /** ECC scheme: "steane" | "surface" | "color_code"  (NULL → "steane") */
    const char *ecc_kind;
    /** Code distance, positive integer                  (0 → 3)           */
    int         ecc_dist;
    /** MLIR function name for QASM import               (NULL → "circuit") */
    const char *func_name;
    /** Comma-separated pass/pipeline names to run       (NULL → no passes) */
    const char *pipeline;
} FTQCCfg;

/**
 * Run the ftqc-opt pass pipeline on MLIR text held in memory.
 *
 * @param mlir_data  Pointer to MLIR text; need not be NUL-terminated.
 * @param mlir_size  Byte length of mlir_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 *                   ecc_kind / ecc_dist / func_name are unused for MLIR input.
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
 * @param spec Pointer to IrisSpec. 
 * @param mlir_data  Pointer to MLIR text; need not be NUL-terminated.
 * @param mlir_size  Byte length of mlir_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 *                   ecc_kind / ecc_dist / func_name are unused for MLIR input.
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_iris_mlir_opt(const void *spec, const void    *mlir_data,
                  size_t         mlir_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
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
 * pipeline on the resulting MLIR.
 *
 * @param spec Pointer to IrisSpec. 
 * @param qasm_data  Pointer to QASM source text; need not be NUL-terminated.
 * @param qasm_size  Byte length of qasm_data.
 * @param cfg        Configuration (may be NULL to use all defaults).
 * @param out_data   On success, receives a malloc'd buffer with output MLIR
 *                   text (NUL-terminated).  Release with ftqc_free().
 * @param out_size   On success, receives the byte length of *out_data
 *                   (excluding the NUL terminator).
 * @return           0 on success, non-zero on failure.
 */
int ftqc_iris_qasm_opt(const void *spec, const void    *qasm_data,
                  size_t         qasm_size,
                  const FTQCCfg *cfg,
                  void         **out_data,
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
