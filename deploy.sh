#!/usr/bin/env bash
# Deploy build artifacts to Guild Wars 2 addons directory for development.
#
# Usage: ./deploy.sh [build_dir]
#
# Requires GW2_PATH environment variable to be set, or a .env file in the
# project root containing GW2_PATH=...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Load .env if present
if [ -f "$SCRIPT_DIR/.env" ]; then
    # shellcheck disable=SC1091
    source "$SCRIPT_DIR/.env"
fi

if [ -z "${GW2_PATH:-}" ]; then
    echo "Error: GW2_PATH is not set." >&2
    echo "Set it as an environment variable or in a .env file." >&2
    exit 1
fi

BUILD_DIR="${1:-$SCRIPT_DIR/build}"
ADDONS_DIR="$GW2_PATH/addons"

if [ ! -f "$BUILD_DIR/nexus_js_loader.dll" ]; then
    echo "Error: Build artifacts not found in $BUILD_DIR" >&2
    echo "Run a build first." >&2
    exit 1
fi

echo "Deploying to $ADDONS_DIR ..."

mkdir -p "$ADDONS_DIR"
cp -f "$BUILD_DIR/nexus_js_loader.dll" "$ADDONS_DIR/"

echo "Done."
