"""lit configuration for the FTQC test suite."""

import lit.formats
import lit.llvm
import os
import shutil

# Initialize the LLVM lit helper — idempotent if the site config already ran it.
lit.llvm.initialize(lit_config, config)
from lit.llvm import llvm_config

config.name = "FTQC"
config.test_format = lit.formats.ShTest(not llvm_config.use_lit_shell)

config.suffixes = [".mlir"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root   = getattr(config, "ftqc_obj_root", config.test_source_root)

# ── Locate ftqc-opt ────────────────────────────────────────────────────────────
# Priority:
#   1. config.ftqc_tools_dir  (set by CMake site config)
#   2. <ftqc_obj_root>/src/   (typical CMake build layout)
#   3. Just "ftqc-opt" on PATH
_ftqc_tools = getattr(config, "ftqc_tools_dir", "")
_ftqc_obj   = getattr(config, "ftqc_obj_root", "")
if not _ftqc_tools or not os.path.isfile(os.path.join(_ftqc_tools, "ftqc-opt")):
    _candidate = os.path.join(_ftqc_obj, "src")
    if os.path.isfile(os.path.join(_candidate, "ftqc-opt")):
        _ftqc_tools = _candidate

ftqc_opt_path = os.path.join(_ftqc_tools, "ftqc-opt") if _ftqc_tools else "ftqc-opt"
config.substitutions.append(("%ftqc-opt", ftqc_opt_path))

# ── Locate LLVM tools (FileCheck, not, count) ──────────────────────────────────
# config.llvm_tools_dir may point to an install/bin that lacks FileCheck.
# Fall back to the directory that contains the running llvm-lit executable,
# which always co-locates with FileCheck in an LLVM build tree.
def _find_llvm_tools_dir(preferred):
    if preferred and os.path.isfile(os.path.join(preferred, "FileCheck")):
        return preferred
    # Check alongside the running lit executable (sys.argv[0]) first,
    # then fall back to PATH search for llvm-lit/lit.
    import sys as _sys
    candidates = [_sys.argv[0],
                  shutil.which("llvm-lit"),
                  shutil.which("lit")]
    for exe in candidates:
        if exe:
            d = os.path.dirname(os.path.realpath(exe))
            if os.path.isfile(os.path.join(d, "FileCheck")):
                return d
    return preferred  # best-effort; will produce a clear "not found" error

_llvm_tools_dir = _find_llvm_tools_dir(config.llvm_tools_dir)
# Patch the config so llvm_config helpers see the corrected path.
config.llvm_tools_dir = _llvm_tools_dir

filecheck_path = getattr(config, "filecheck_path",
                         os.path.join(_llvm_tools_dir, "FileCheck"))
if not os.path.isfile(filecheck_path):
    filecheck_path = os.path.join(_llvm_tools_dir, "FileCheck")
config.substitutions.append(("%FileCheck", filecheck_path))

# ── Standard LLVM substitutions ────────────────────────────────────────────────
# NOTE: with_system_environment(["PATH"]) would overwrite any PATH changes we
# make here, so we omit PATH from that call and update it ourselves afterwards.
llvm_config.with_system_environment(["HOME"])
llvm_config.use_default_substitutions()
llvm_config.add_tool_substitutions(["FileCheck", "not", "count"], _llvm_tools_dir)

# ── Prepend FTQC tools to PATH (must come after with_system_environment) ───────
# Tests call 'ftqc-opt' by name rather than via %ftqc-opt substitution, so
# the binary directory must appear on the PATH seen by the test subprocess.
_base_path = os.environ.get("PATH", "")
if _ftqc_tools:
    config.environment["PATH"] = _ftqc_tools + os.pathsep + _base_path
else:
    config.environment["PATH"] = _base_path
