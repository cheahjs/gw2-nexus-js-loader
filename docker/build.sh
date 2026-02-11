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

# Make build directory world-writable so the Docker wine user (different UID
# than the host) can create subdirectories and write files on the bind mount
mkdir -p "$PROJECT_DIR/build"
chmod 777 "$PROJECT_DIR/build"

docker run --rm \
    --platform linux/amd64 \
    -v "$PROJECT_DIR:/project" \
    --entrypoint /bin/bash \
    "$DOCKER_IMAGE" \
    -c '
        set -e

        echo "=== Step 1: CMake Configure ==="
        # Write batch file for cmake configure
        cat > /home/wine/.wine/drive_c/configure.bat << "BATEOF"
@echo off
call C:\x64.bat
cmake.exe -G Ninja -B Z:\project\build -S Z:\project -DCMAKE_BUILD_TYPE='"${BUILD_TYPE}"'
BATEOF
        wine64 cmd /c C:\\configure.bat

        echo ""
        echo "=== Step 2: Fix Wine/Rosetta null-byte corruption ==="
        find /project/build -name "*.ninja" -exec sed -i "s/\x00//g" {} +

        echo ""
        echo "=== Step 3: Generate compile database ==="
        # Generate compile commands database from ninja
        cat > /home/wine/.wine/drive_c/compdb.bat << "BATEOF"
@echo off
call C:\x64.bat
cd /d Z:\project\build
C:\Tools\Ninja\ninja.exe -t compdb > C:\compdb.json
BATEOF
        wine64 cmd /c C:\\compdb.bat

        echo ""
        echo "=== Step 4: Build using wine_build.py ==="
        python3 /project/docker/wine_build.py
    '

echo ""
echo "=== Build complete ==="
echo "Output: $PROJECT_DIR/build/"
ls -la "$PROJECT_DIR/build/"*.dll "$PROJECT_DIR/build/"*.exe "$PROJECT_DIR/build/"*.lib 2>/dev/null || echo "(check build/ for output files)"
