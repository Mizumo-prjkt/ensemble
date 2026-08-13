#!/bin/bash

set -e

BUILD_TYPE="Debug"

# Support './build.sh release' or './build.sh --release'
if [ "$1" = "release" ] || [ "$1" = "--release" ] || [ "$BUILD_TYPE_ENV" = "Release" ]; then
    BUILD_TYPE="Release"
fi

echo "========================================"
echo " Building Ensemble in ${BUILD_TYPE} mode"
echo "========================================"

cmake -B build -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
cmake --build build --parallel

echo "Launching Ensemble (${BUILD_TYPE})..."
./build/ensemble