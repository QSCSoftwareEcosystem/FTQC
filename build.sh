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

set -euo pipefail
set -x
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

LLVM_INSTALL="${LLVM_INSTALL:-${SCRIPT_DIR}/../llvm-project.c014/install}"
MLIR_DIR="${MLIR_DIR:-${LLVM_INSTALL}/lib/cmake/mlir}"
LLVM_DIR="${LLVM_DIR:-${LLVM_INSTALL}/lib/cmake/llvm}"
LIT="${LIT:-${LLVM_INSTALL}/../build/bin/llvm-lit}"
PREFIX="${PREFIX:-${SCRIPT_DIR}/install}"
BUILD_DIR="${BUILD_DIR:-${PWD}}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_TESTS="${BUILD_TESTS:-ON}"
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
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
else
  echo "==> Build dir already configured — skipping cmake (use --clean to reconfigure)"
fi

# ── Build ─────────────────────────────────────────────────────────────────────
echo "==> Building with ${JOBS} jobs"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo ""
echo "Binary: ${BUILD_DIR}/src/ftqc-opt"
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
  if [[ -z "$LIT" ]]; then
    echo "WARNING: llvm-lit not found; skipping tests"
  else
    "$LIT" "${BUILD_DIR}/test" -v
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
