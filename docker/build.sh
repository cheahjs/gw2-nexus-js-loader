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

# Write a batch file that cmake will execute inside Wine.
# The project is mounted at /project (Linux) = Z:\project (Wine).
BATCH_FILE="$PROJECT_DIR/build.bat"

cat > "$BATCH_FILE" <<EOF
@echo off
call C:\\x64.bat
cmake.exe -G Ninja -B Z:\\project\\build -S Z:\\project -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
EOF

docker run --rm \
    --platform linux/amd64 \
    -v "$PROJECT_DIR:/project" \
    --entrypoint /bin/bash \
    "$DOCKER_IMAGE" \
    -c '
        # Configure step
        wine64 cmd /c Z:\\project\\build.bat

        # Fix Wine/Rosetta null-byte corruption in Ninja build files
        find /project/build -name "build.ninja" -exec sed -i "s/\x00//g" {} +

        # Build step
        wine64 cmd /c "C:\x64.bat && cmake.exe --build Z:\project\build --config '"${BUILD_TYPE}"'"
    '

rm -f "$BATCH_FILE"

echo ""
echo "=== Build complete ==="
echo "Output: $PROJECT_DIR/build/"
ls -la "$PROJECT_DIR/build/"*.dll "$PROJECT_DIR/build/"*.exe 2>/dev/null || echo "(check build/ for output files)"
