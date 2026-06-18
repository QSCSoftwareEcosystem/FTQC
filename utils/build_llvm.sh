#wget https://github.com/llvm/llvm-project/releases/download/llvmorg-20.1.3/llvm-project-20.1.3.src.tar.xz
#tar -xf llvm-project-20.1.3.src.tar.xz
#cd llvm-project-20.1.3.src
#cd llvm/projects
#git clone https://github.com/JuliaHubOSS/llvm-cbe
#cd ../../
if [ ! -d "llvm-project/.git" ]; then
    git init llvm-project
    cd llvm-project
    git remote add origin https://github.com/llvm/llvm-project.git
    git fetch --depth 1 origin 113f01aa82d055410f22a9d03b3468fa68600589
    git checkout FETCH_HEAD
else
    cd llvm-project
fi
set -e
mkdir -p build ; cd build ;
BUILD_TYPE=${BUILD_TYPE:-Release}
PYTHON=${PYTHON:-python3}
C_COMPILER=${C_COMPILER:-gcc}
CXX_COMPILER=${CXX_COMPILER:-g++}
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
if [ "$(uname)" = "Darwin" ]; then
    LLVM_PROJECTS="${LLVM_PROJECTS:-clang;clang-tools-extra;mlir}"
    MACOS_SDK=$(xcrun --sdk macosx --show-sdk-path)
    MACOS_SYSROOT="-DCMAKE_OSX_SYSROOT=${MACOS_SDK}"
    # macOS 26+ moved C++ stdlib headers into the SDK; add explicitly if missing from CLT
    if [ ! -f "/Library/Developer/CommandLineTools/usr/include/c++/v1/atomic" ]; then
        MACOS_CXX_FLAGS="-DCMAKE_CXX_FLAGS=-I${MACOS_SDK}/usr/include/c++/v1"
    else
        MACOS_CXX_FLAGS=""
    fi
else
    LLVM_PROJECTS="${LLVM_PROJECTS:-clang;clang-tools-extra;openmp;mlir}"
    MACOS_SYSROOT=""
    MACOS_CXX_FLAGS=""
fi
LLVM_BUILD_DIR="${LLVM_BUILD_DIR:-$PWD}"
LLVM_TARGETS="${LLVM_TARGETS:-check-mlir llvm-symbolizer}"
cmake -G Ninja -S ../llvm -B ${LLVM_BUILD_DIR} \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
    ${MACOS_SYSROOT} \
    ${MACOS_CXX_FLAGS} \
    -DMLIR_BUILD_HLO=ON \
    -DMLIR_BUILD_HLO_TESTS=ON \
    -DMLIR_BUILD_STABLEHLO=ON \
    -DMLIR_BUILD_STABLEHLO_TESTS=ON \
    -DLLVM_ENABLE_RTTI=ON -DLLVM_ENABLE_EH=ON \
    -DLLVM_BUILD_EXAMPLES=OFF \
    -DLLVM_TARGETS_TO_BUILD="host" \
    -DLLVM_ENABLE_PROJECTS="${LLVM_PROJECTS}" \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DMLIR_ENABLE_BINDINGS_PYTHON=ON \
    -DPython_EXECUTABLE=${PYTHON} \
    -DPython3_EXECUTABLE=${PYTHON} \
    -DPython3_NumPy_INCLUDE_DIRS=`python3 -c "import numpy as np; print(np.get_include())"` \
    -Dpybind11_DIR=`python3 -c "import pybind11; print(pybind11.get_cmake_dir())"` \
    -Dnanobind_DIR=`python3 -c "import nanobind; print(nanobind.cmake_dir())"` \
    -DCMAKE_C_COMPILER=${C_COMPILER} \
    -DCMAKE_CXX_COMPILER=${CXX_COMPILER} \
    -DLLVM_USE_SANITIZER="" \
    -DLLVM_ENABLE_LLD=OFF \
    -DLLVM_ENABLE_ZLIB=ON \
    -DLLVM_INSTALL_UTILS=ON \
    -DLLVM_ENABLE_ZSTD=OFF \
    -DCMAKE_CXX_VISIBILITY_PRESET=default
#-DCMAKE_C_COMPILER_LAUNCHER=$(COMPILER_LAUNCHER) \
#-DCMAKE_CXX_COMPILER_LAUNCHER=$(COMPILER_LAUNCHER) \

set +e
LIT_FILTER_OUT="Bytecode|tosa-to-tensor|execution_engine" cmake --build ${LLVM_BUILD_DIR} --target ${LLVM_TARGETS}
set -e
cmake --build ${LLVM_BUILD_DIR} --target install
#ninja llvm-cbe
