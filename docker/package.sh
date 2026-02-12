#!/usr/bin/env bash
# Assembles a release ZIP from build artifacts.
#
# Usage: docker/package.sh [build_dir] [cef_dist_dir] [output_zip]
#
# Defaults:
#   build_dir    = build
#   cef_dist_dir = third_party/cef/cef_dist
#   output_zip   = nexus-js-loader.zip

set -euo pipefail

BUILD_DIR="${1:-build}"
CEF_DIST="${2:-third_party/cef/cef_dist}"
OUTPUT_ZIP="${3:-nexus-js-loader.zip}"

STAGING="$(mktemp -d)"
trap 'rm -rf "$STAGING"' EXIT

SUBFOLDER="$STAGING/nexus_js_loader"
mkdir -p "$SUBFOLDER/locales"

# Main DLL at root
cp "$BUILD_DIR/nexus_js_loader.dll" "$STAGING/"

# CEF host process in subfolder
cp "$BUILD_DIR/nexus_js_cef_host.exe" "$SUBFOLDER/"

# Subprocess in subfolder
cp "$BUILD_DIR/nexus_js_subprocess.exe" "$SUBFOLDER/"

# CEF core DLLs from Release/
for f in libcef.dll chrome_elf.dll v8_context_snapshot.bin \
         d3dcompiler_47.dll libEGL.dll libGLESv2.dll \
         vk_swiftshader.dll vulkan-1.dll dxcompiler.dll dxil.dll; do
    [ -f "$CEF_DIST/Release/$f" ] && cp "$CEF_DIST/Release/$f" "$SUBFOLDER/"
done

# vk_swiftshader_icd.json
[ -f "$CEF_DIST/Release/vk_swiftshader_icd.json" ] && \
    cp "$CEF_DIST/Release/vk_swiftshader_icd.json" "$SUBFOLDER/"

# CEF resource files
for f in icudtl.dat chrome_100_percent.pak chrome_200_percent.pak resources.pak; do
    cp "$CEF_DIST/Resources/$f" "$SUBFOLDER/"
done

# Locales
cp "$CEF_DIST/Resources/locales/"* "$SUBFOLDER/locales/"

# Create ZIP
(cd "$STAGING" && zip -r - .) > "$OUTPUT_ZIP"

echo "Created $OUTPUT_ZIP"
echo "Contents:"
unzip -l "$OUTPUT_ZIP" | tail -n +4 | head -20
echo "..."
