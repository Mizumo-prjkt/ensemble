#!/bin/bash

set -e

BUILD_TYPE="Debug"
CROSS_WIN=false

for arg in "$@"; do
    case "$arg" in
        release|--release)
            BUILD_TYPE="Release"
            ;;
        win|mingw|--win)
            CROSS_WIN=true
            ;;
    esac
done

if [ "$CROSS_WIN" = true ]; then
    echo "=================================================="
    echo " Cross-Compiling Ensemble for Windows (${BUILD_TYPE})"
    echo "=================================================="
    
    if [ ! -f "cmake/x86_64-w64-mingw32.cmake" ]; then
        mkdir -p cmake
        cat << 'EOF' > cmake/x86_64-w64-mingw32.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
EOF
    fi

    cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-w64-mingw32.cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
    cmake --build build-win --parallel
    echo "Windows build binary generated at: ./build-win/ensemble.exe"
else
    echo "========================================"
    echo " Building Ensemble in ${BUILD_TYPE} mode"
    echo "========================================"

    cmake -B build -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
    cmake --build build --parallel

    echo "Launching Ensemble (${BUILD_TYPE})..."
    ./build/ensemble
fi