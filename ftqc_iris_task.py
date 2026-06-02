#!/usr/bin/env python3
"""
ftqc_iris_task.py — IRIS dmem + @iris.kernel pipeline: QASM → physical circuit

Two @iris.kernel tasks exchange data through iris.dmem buffers:

  Task 1 (ftqc_qasm_import)   ftqc_qasm_opt   QASM bytes → logical MLIR bytes
  Task 2 (ftqc_mlir_opt_fn)   ftqc_mlir_opt   logical MLIR bytes → physical MLIR bytes

IRIS data flow:
  qasm_dmem ──h2d──► [ftqc_qasm_import] ──flush──► logical_dmem
                                                         │ h2d
                                                         ▼
                                              [ftqc_mlir_opt_fn] ──flush──► physical_dmem

Usage:
  source /Users/nqx/Development/iris.machq/install/setup.source
  python3 ftqc_iris_task.py [input.qasm] [options]

Options:
  --ecc-kind   steane | surface | color_code   (default: steane)
  --ecc-dist   code distance                   (default: 3)
  --func-name  MLIR function name              (default: circuit)
  --pipeline   physical pass pipeline          (default: ftqc-steane-pipeline)
  --out        output MLIR file                (default: <input>.physical.mlir)
"""

import sys, os, argparse, ctypes
from ctypes import c_int, c_size_t, c_void_p, POINTER, Structure
from pathlib import Path

import numpy as np

# ─── IRIS ─────────────────────────────────────────────────────────────────────

_IRIS_INSTALL = Path(os.environ.get("IRIS",
    "/Users/nqx/Development/iris.machq/install"))
sys.path.insert(0, str(_IRIS_INSTALL))
import iris

# ─── FTQC shared library ──────────────────────────────────────────────────────

_HERE    = Path(__file__).parent
_LIBFTQC = _HERE / "build" / "src" / "libftqc.dylib"

class FTQCCfg(Structure):
    _fields_ = [
        ("ecc_kind",  ctypes.c_char_p),
        ("ecc_dist",  c_int),
        ("func_name", ctypes.c_char_p),
        ("pipeline",  ctypes.c_char_p),
    ]

_lib = ctypes.CDLL(str(_LIBFTQC))
_lib.ftqc_qasm_opt.argtypes = [c_void_p, c_size_t, POINTER(FTQCCfg),
                                POINTER(c_void_p), POINTER(c_size_t)]
_lib.ftqc_qasm_opt.restype  = c_int
_lib.ftqc_mlir_opt.argtypes = [c_void_p, c_size_t, POINTER(FTQCCfg),
                                POINTER(c_void_p), POINTER(c_size_t)]
_lib.ftqc_mlir_opt.restype  = c_int
_lib.ftqc_free.argtypes     = [c_void_p]
_lib.ftqc_free.restype      = None

_MAX_OUT = 1 << 19   # 512 KB pre-allocated output buffer

# ─── C-API wrapper ────────────────────────────────────────────────────────────

def _call_opt(fn, in_np, in_size, cfg, out_np, size_np):
    """Call ftqc_qasm_opt or ftqc_mlir_opt into a pre-allocated numpy buffer."""
    out_data = c_void_p()
    out_size = c_size_t()
    rc = fn(in_np.ctypes.data_as(c_void_p), c_size_t(in_size),
            ctypes.byref(cfg), ctypes.byref(out_data), ctypes.byref(out_size))
    if rc != 0:
        return rc
    n = out_size.value
    if n > len(out_np):
        _lib.ftqc_free(out_data)
        return -1   # buffer too small
    out_np[:n] = np.frombuffer(ctypes.string_at(out_data, n), dtype=np.uint8)
    size_np[0] = n
    _lib.ftqc_free(out_data)
    return 0

# ─── @iris.kernel task factories ─────────────────────────────────────────────
#
# in_   : read-only numpy params  → h2d before kernel
# flush_: write numpy params      → d2h (flush) after kernel
#
# FTQCCfg is captured via closure so it never enters the IRIS scalar pipeline
# (iris.get_scalar_conv only handles numeric types and str).

def make_qasm_kernel_i(cfg):
    """Return an @iris.kernel for ftqc_qasm_opt with cfg captured by closure."""
    @iris.kernel(in_=["qasm_in"], flush_=["mlir_out", "mlir_size"])
    def ftqc_iris_qasm_opt(spec, qasm_in, len_qasm_in, cfg, mlir_out, mlir_size):
        pass
    def call(mlir_out, mlir_size, qasm_in):
        return ftqc_iris_qasm_opt(qasm_in, np.int64(len(qasm_in)), cfg, mlir_out, mlir_size, gws=[1])
    return call


def make_mlir_kernel_i(cfg):
    """Return an @iris.kernel for ftqc_mlir_opt with cfg captured by closure."""
    @iris.kernel(in_=["mlir_in", "mlir_in_size"], flush_=["mlir_out", "mlir_out_size"])
    def ftqc_mlir_opt_fn(spec, mlir_out, mlir_out_size, mlir_in, mlir_in_size):
        in_size = int(mlir_in_size[0])
        rc = _call_opt(_lib.ftqc_mlir_opt,
                       mlir_in, in_size, cfg, mlir_out, mlir_out_size)
        if rc != 0:
            raise RuntimeError(f"ftqc_mlir_opt rc={rc}")
        print(f"  [task2:mlir_opt]     {in_size} B  →"
              f"  {int(mlir_out_size[0])} B  physical MLIR")
    return ftqc_mlir_opt_fn

def make_qasm_kernel(cfg):
    """Return an @iris.kernel for ftqc_qasm_opt with cfg captured by closure."""
    @iris.kernel(in_=["qasm_in"], flush_=["mlir_out", "mlir_size"])
    def ftqc_qasm_import(spec, mlir_out, mlir_size, qasm_in):
        rc = _call_opt(_lib.ftqc_qasm_opt,
                       qasm_in, len(qasm_in), cfg, mlir_out, mlir_size)
        if rc != 0:
            raise RuntimeError(f"ftqc_qasm_opt rc={rc}")
        print(f"  [task1:qasm_import]  {len(qasm_in)} B  →"
              f"  {int(mlir_size[0])} B  logical MLIR")
    return ftqc_qasm_import


def make_mlir_kernel(cfg):
    """Return an @iris.kernel for ftqc_mlir_opt with cfg captured by closure."""
    @iris.kernel(in_=["mlir_in", "mlir_in_size"], flush_=["mlir_out", "mlir_out_size"])
    def ftqc_mlir_opt_fn(spec, mlir_out, mlir_out_size, mlir_in, mlir_in_size):
        in_size = int(mlir_in_size[0])
        rc = _call_opt(_lib.ftqc_mlir_opt,
                       mlir_in, in_size, cfg, mlir_out, mlir_out_size)
        if rc != 0:
            raise RuntimeError(f"ftqc_mlir_opt rc={rc}")
        print(f"  [task2:mlir_opt]     {in_size} B  →"
              f"  {int(mlir_out_size[0])} B  physical MLIR")
    return ftqc_mlir_opt_fn

# ─── Pipeline ─────────────────────────────────────────────────────────────────

def run(qasm_file: Path, ecc_kind: str, ecc_dist: int, func_name: str,
        pipeline: str, out_file: Path) -> None:

    qasm_bytes = qasm_file.read_bytes()

    # ── Numpy backing arrays ──────────────────────────────────────────────────
    qasm_np          = np.frombuffer(qasm_bytes, dtype=np.uint8).copy()
    logical_np       = np.zeros(_MAX_OUT, dtype=np.uint8)
    logical_size_np  = np.zeros(2, dtype=np.uint64)  # [0]=size; [1] keeps nelems>1 so IRIS passes as array
    physical_np      = np.zeros(_MAX_OUT, dtype=np.uint8)
    physical_size_np = np.zeros(2, dtype=np.uint64)  # [0]=size; [1] keeps nelems>1

    # ── FTQCCfg ───────────────────────────────────────────────────────────────
    cfg_import = FTQCCfg(ecc_kind=ecc_kind.encode(), ecc_dist=ecc_dist,
                         func_name=func_name.encode(), pipeline=b"")
    cfg_opt    = FTQCCfg(ecc_kind=ecc_kind.encode(), ecc_dist=ecc_dist,
                         func_name=func_name.encode(), pipeline=pipeline.encode())

    iris.init()

    # ── Register buffers as iris.dmem (must be after iris.init) ──────────────
    iris.dmem(qasm_np)
    iris.dmem(logical_np)
    iris.dmem(logical_size_np)
    iris.dmem(physical_np)
    iris.dmem(physical_size_np)

    # ── Build kernels (cfg captured by closure) ───────────────────────────────
    k_qasm = make_qasm_kernel(cfg_import)
    k_mlir = make_mlir_kernel(cfg_opt)

    # ── Task 1: QASM → logical MLIR ──────────────────────────────────────────
    t1 = k_qasm(logical_np, logical_size_np, qasm_np)
    t1.submit(iris.iris_cpu, sync=1)

    if logical_size_np[0] == 0:
        iris.finalize()
        raise RuntimeError("Task 1 produced no output")

    # ── Task 2: logical MLIR → physical circuit ───────────────────────────────
    t2 = k_mlir(physical_np, physical_size_np, logical_np, logical_size_np,
                 gws=[1])
    t2.submit(iris.iris_cpu, sync=1)

    iris.finalize()

    if physical_size_np[0] == 0:
        raise RuntimeError("Task 2 produced no output")

    # ── Write output ──────────────────────────────────────────────────────────
    n        = int(physical_size_np[0])
    out_text = physical_np[:n].tobytes().decode()
    out_file.write_text(out_text)

    print(f"\nOutput: {out_file}  ({n} chars)")
    preview = out_text[:3000]
    if n > 3000:
        preview += f"\n... [{n - 3000} chars truncated]"
    print(preview)

# ─── CLI ──────────────────────────────────────────────────────────────────────

def main():
    default_qasm = str(_HERE / "test" / "Integration" / "bell.qasm")

    ap = argparse.ArgumentParser(
        description="IRIS @iris.kernel FTQC QASM → physical circuit pipeline")
    ap.add_argument("qasm_file", nargs="?", default=default_qasm)
    ap.add_argument("--ecc-kind",  default="steane",
                    choices=["steane", "surface", "color_code"])
    ap.add_argument("--ecc-dist",  type=int, default=3)
    ap.add_argument("--func-name", default="circuit")
    ap.add_argument("--pipeline",  default="ftqc-steane-pipeline")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    qasm_file = Path(args.qasm_file)
    if not qasm_file.exists():
        print(f"Error: {qasm_file} not found", file=sys.stderr)
        sys.exit(1)

    out_file = Path(args.out) if args.out else qasm_file.with_suffix(".physical.mlir")

    print(f"Input:    {qasm_file}")
    print(f"ECC:      {args.ecc_kind}  d={args.ecc_dist}")
    print(f"Pipeline: {args.pipeline}")
    print(f"Output:   {out_file}")
    print()

    run(qasm_file, args.ecc_kind, args.ecc_dist, args.func_name,
        args.pipeline, out_file)

if __name__ == "__main__":
    main()
