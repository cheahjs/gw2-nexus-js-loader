#!/usr/bin/env bash
# Assembles a release ZIP from build artifacts.
#
# In-process CEF architecture: only the DLL is needed.
# No CEF runtime files or executables — uses GW2's already-loaded libcef.dll.
#
# Usage: docker/package.sh [build_dir] [output_zip]
#
# Defaults:
#   build_dir  = build
#   output_zip = nexus-js-loader.zip

set -euo pipefail

BUILD_DIR="${1:-build}"
OUTPUT_ZIP="${2:-nexus-js-loader.zip}"

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

# Main DLL — the only output needed
cp "$BUILD_DIR/nexus_js_loader.dll" "$STAGING/"

# Create ZIP
(cd "$STAGING" && zip -r - .) > "$OUTPUT_ZIP"

echo "Created $OUTPUT_ZIP"
echo "Contents:"
unzip -l "$OUTPUT_ZIP"
