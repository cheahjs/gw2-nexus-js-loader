#!/bin/bash
# Cross-compile build script using Docker with Wine + MSVC toolchain
# Usage: ./docker/build.sh [Release|Debug]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_TYPE="${1:-Release}"

DOCKER_IMAGE="madduci/docker-wine-msvc:17.8-2022"

echo "=== GW2 Nexus JS Loader - Docker Build ==="
echo "Project dir: $PROJECT_DIR"
echo "Build type:  $BUILD_TYPE"
echo "Docker image: $DOCKER_IMAGE"
echo ""

docker run --rm \
    -v "$PROJECT_DIR:/project" \
    "$DOCKER_IMAGE" \
    bash -c "cmake -G Ninja -B /project/build -S /project \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DCMAKE_SYSTEM_NAME=Windows \
        && cmake --build /project/build --config $BUILD_TYPE"

echo ""
echo "=== Build complete ==="
echo "Output: $PROJECT_DIR/build/"
ls -la "$PROJECT_DIR/build/"*.dll "$PROJECT_DIR/build/"*.exe 2>/dev/null || echo "(check build/Release/ or build/ for output files)"
