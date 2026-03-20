#!/usr/bin/env bash
# build.sh — Configure, build, and optionally install ftqc-opt.
#
# Usage:
#   ./build.sh                  # configure + build (Release)
#   ./build.sh --debug          # Debug build
#   ./build.sh --install        # build + install to PREFIX
#   ./build.sh --clean          # wipe build dir, then configure + build
#   ./build.sh --tests          # build + run lit test suite
#   ./build.sh --docs           # generate Doxygen API docs (docs/html/)
#   ./build.sh --help
#
# Key variables (override via environment):
#   MLIR_DIR   path to an MLIR cmake directory
#              default: llvm-project.c014/install/lib/cmake/mlir
#   PREFIX     installation prefix  (default: ./install)
#   BUILD_DIR  build directory      (default: ./build)
#   JOBS       parallel jobs        (default: nproc)
#   TRACE      set to 1 to enable shell command tracing (default: 0)

set -euo pipefail
TRACE="${TRACE:-0}"
if [[ "${TRACE}" == "1" ]]; then
  set -x
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LLVM_INSTALL="${LLVM_INSTALL:-${SCRIPT_DIR}/../llvm-project.c014/install}"

# Allow explicit env overrides first; otherwise auto-detect common and generic layouts.
MLIR_DIR="${MLIR_DIR:-}"
LLVM_DIR="${LLVM_DIR:-}"

pick_first_dir_with_cfg() {
  local __outvar="$1"; shift
  local cfg1="$1"; shift
  local cfg2="$1"; shift
  local d
  for d in "$@"; do
    [[ -z "${d}" ]] && continue
    if [[ -f "${d}/${cfg1}" || -f "${d}/${cfg2}" ]]; then
      printf -v "${__outvar}" '%s' "${d}"
      return 0
    fi
  done
  return 1
}

if [[ -z "${MLIR_DIR}" ]]; then
  MLIR_CANDIDATES=(
    "${LLVM_INSTALL}/lib/cmake/mlir"
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/build/lib/cmake/mlir"
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/install/lib/cmake/mlir"
    "/usr/lib/cmake/mlir"
    "/usr/local/lib/cmake/mlir"
    "/opt/lib/cmake/mlir"
    "/opt/local/lib/cmake/mlir"
    "/usr/lib/llvm-*/lib/cmake/mlir"
    "/usr/local/opt/llvm/lib/cmake/mlir"
    "/opt/homebrew/opt/llvm/lib/cmake/mlir"
  )

  if command -v llvm-config &>/dev/null; then
    LLVM_CONFIG_CMAKEDIR="$(llvm-config --cmakedir 2>/dev/null || true)"
    if [[ -n "${LLVM_CONFIG_CMAKEDIR}" ]]; then
      MLIR_CANDIDATES+=("${LLVM_CONFIG_CMAKEDIR%/llvm}/mlir")
    fi
  fi

  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    IFS=':' read -r -a _cmake_prefixes <<< "${CMAKE_PREFIX_PATH}"
    for prefix in "${_cmake_prefixes[@]}"; do
      [[ -z "${prefix}" ]] && continue
      MLIR_CANDIDATES+=(
        "${prefix}/lib/cmake/mlir"
        "${prefix}/lib64/cmake/mlir"
        "${prefix}/share/mlir/cmake"
      )
    done
  fi

  pick_first_dir_with_cfg MLIR_DIR "MLIRConfig.cmake" "mlir-config.cmake" "${MLIR_CANDIDATES[@]}" || true
fi

if [[ -z "${LLVM_DIR}" ]]; then
  LLVM_CANDIDATES=(
    "${LLVM_INSTALL}/lib/cmake/llvm"
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/build/lib/cmake/llvm"
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/install/lib/cmake/llvm"
    "/usr/lib/cmake/llvm"
    "/usr/local/lib/cmake/llvm"
    "/opt/lib/cmake/llvm"
    "/opt/local/lib/cmake/llvm"
    "/usr/lib/llvm-*/lib/cmake/llvm"
    "/usr/local/opt/llvm/lib/cmake/llvm"
    "/opt/homebrew/opt/llvm/lib/cmake/llvm"
  )

  if [[ -n "${MLIR_DIR}" ]]; then
    LLVM_CANDIDATES+=("${MLIR_DIR%/mlir}/llvm")
  fi

  if command -v llvm-config &>/dev/null; then
    LLVM_CONFIG_CMAKEDIR="$(llvm-config --cmakedir 2>/dev/null || true)"
    if [[ -n "${LLVM_CONFIG_CMAKEDIR}" ]]; then
      LLVM_CANDIDATES+=("${LLVM_CONFIG_CMAKEDIR}")
    fi
  fi

  if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
    IFS=':' read -r -a _cmake_prefixes <<< "${CMAKE_PREFIX_PATH}"
    for prefix in "${_cmake_prefixes[@]}"; do
      [[ -z "${prefix}" ]] && continue
      LLVM_CANDIDATES+=(
        "${prefix}/lib/cmake/llvm"
        "${prefix}/lib64/cmake/llvm"
        "${prefix}/share/llvm/cmake"
      )
    done
  fi

  pick_first_dir_with_cfg LLVM_DIR "LLVMConfig.cmake" "llvm-config.cmake" "${LLVM_CANDIDATES[@]}" || true
fi
LIT="${LIT:-}"
if [[ -z "${LIT}" ]]; then
  LIT_FROM_LLVM_DIR=""
  if [[ -n "${LLVM_DIR}" ]]; then
    LIT_FROM_LLVM_DIR="$(dirname "$(dirname "$(dirname "${LLVM_DIR}")")")/bin/llvm-lit"
  fi
  for lit_candidate in \
    "${LIT_FROM_LLVM_DIR}" \
    "${LLVM_INSTALL}/../build/bin/llvm-lit" \
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/build/bin/llvm-lit" \
    "${SCRIPT_DIR}/../catalyst/mlir/llvm-project/install/bin/llvm-lit"
  do
    if [[ -x "${lit_candidate}" ]]; then
      LIT="${lit_candidate}"
      break
    fi
  done
  if [[ -z "${LIT}" ]] && command -v llvm-lit &>/dev/null; then
    LIT="$(command -v llvm-lit)"
  fi
fi
PREFIX="${PREFIX:-${SCRIPT_DIR}/install}"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_TESTS="${BUILD_TESTS:-ON}"
SUPPRESS_UNUSED_WARNINGS="${SUPPRESS_UNUSED_WARNINGS:-ON}"

# Validate that required package dirs are discoverable before running CMake.
if [[ ! -f "${MLIR_DIR}/MLIRConfig.cmake" && ! -f "${MLIR_DIR}/mlir-config.cmake" ]]; then
  echo "ERROR: Could not find MLIRConfig.cmake."
  echo "Set MLIR_DIR explicitly, e.g.:"
  echo "  MLIR_DIR=/path/to/lib/cmake/mlir LLVM_DIR=/path/to/lib/cmake/llvm ./build.sh --clean"
  exit 1
fi
if [[ ! -f "${LLVM_DIR}/LLVMConfig.cmake" && ! -f "${LLVM_DIR}/llvm-config.cmake" ]]; then
  echo "ERROR: Could not find LLVMConfig.cmake."
  echo "Set LLVM_DIR explicitly, e.g.:"
  echo "  MLIR_DIR=/path/to/lib/cmake/mlir LLVM_DIR=/path/to/lib/cmake/llvm ./build.sh --clean"
  exit 1
fi
# ── Defaults ──────────────────────────────────────────────────────────────────

INSTALL=0
CLEAN=0
RUN_TESTS=0
BUILD_DOCS=0

# ── Argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
  case "$arg" in
    --debug)   BUILD_TYPE=Debug ;;
    --install) INSTALL=1 ;;
    --clean)   CLEAN=1 ;;
    --tests)   RUN_TESTS=1 ;;
    --docs)    BUILD_DOCS=1 ;;
    --help|-h)
      sed -n '2,15p' "${BASH_SOURCE[0]}" | sed 's/^# //'
      exit 0 ;;
    *) echo "Unknown argument: $arg  (try --help)"; exit 1 ;;
  esac
done

# ── Clean ─────────────────────────────────────────────────────────────────────
if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
  echo "==> Removing ${BUILD_DIR}"
  rm -rf "$BUILD_DIR"
fi

# ── CMake configure ───────────────────────────────────────────────────────────
if [[ ! -f "${BUILD_DIR}/build.ninja" && ! -f "${BUILD_DIR}/Makefile" ]]; then
  echo "==> Configuring (${BUILD_TYPE})"
#-G Ninja \
  cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
    -DMLIR_DIR="${MLIR_DIR}" \
    -DLLVM_DIR="${LLVM_DIR}" \
    -DFTQC_BUILD_TESTS="${BUILD_TESTS}" \
    -DFTQC_SUPPRESS_UNUSED_WARNINGS="${SUPPRESS_UNUSED_WARNINGS}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
  echo "==> Build dir already configured — skipping cmake (use --clean to reconfigure)"
fi

# ── Build ─────────────────────────────────────────────────────────────────────
echo "==> Building with ${JOBS} jobs"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo "Binary: ${BUILD_DIR}/bin/ftqc-opt"
"${BUILD_DIR}/src/ftqc-opt" --version 2>&1 | head -1 || true

# ── Install ───────────────────────────────────────────────────────────────────
if [[ $INSTALL -eq 1 ]]; then
  echo ""
  echo "==> Installing to ${PREFIX}"
  cmake --install "$BUILD_DIR"
  echo "Installed: ${PREFIX}/bin/ftqc-opt"
fi

# ── Tests ─────────────────────────────────────────────────────────────────────
if [[ $RUN_TESTS -eq 1 ]]; then
  echo ""
  echo "==> Running lit test suite"
  if [[ -z "$LIT" || ! -x "$LIT" ]]; then
    echo "WARNING: llvm-lit not found/executable; skipping tests"
    echo "Hint: set LIT=/path/to/llvm-lit"
  else
    echo "Using llvm-lit: ${LIT}"
    if ! "$LIT" "${BUILD_DIR}/test" -v; then
      if command -v python3 &>/dev/null; then
        echo "llvm-lit wrapper failed; retrying with python3 ${LIT}"
        python3 "$LIT" "${BUILD_DIR}/test" -v
      else
        echo "ERROR: llvm-lit failed and python3 is not available for fallback"
        exit 1
      fi
    fi
  fi
fi

# ── Docs ──────────────────────────────────────────────────────────────────────
if [[ $BUILD_DOCS -eq 1 ]]; then
  echo ""
  if cmake --build "$BUILD_DIR" --target docs 2>/dev/null; then
    echo "==> Documentation written to ${SCRIPT_DIR}/docs/html/index.html"
  else
    # Fall back to running Doxygen directly if the CMake target is unavailable.
    if command -v doxygen &>/dev/null; then
      echo "==> Running Doxygen directly"
      doxygen "${SCRIPT_DIR}/docs/Doxyfile"
      echo "==> Documentation written to ${SCRIPT_DIR}/docs/html/index.html"
    else
      echo "WARNING: Doxygen not found; skipping docs generation"
    fi
  fi
fi

echo ""
echo "Done."
