#!/usr/bin/env bash
# Local build helper for the SC1000 plugin (AU + Standalone, macOS/arm64).
# JUCE is pulled via CMake FetchContent into build/ — nothing is installed globally.
#
# If this machine's Command Line Tools libc++ is broken (missing <algorithm> etc.),
# this points clang at the SDK's intact libc++ headers — but only when actually
# broken, so it's a no-op on a healthy toolchain. The real fix for that breakage is:
#   sudo rm -rf /Library/Developer/CommandLineTools && sudo xcode-select --install
#
# Usage: ./build.sh [Debug|Release]   (default: Debug)
set -euo pipefail
cd "$(dirname "$0")"

BUILD_TYPE="${1:-Debug}"
BUILD_DIR="build"

# Detect a broken toolchain libc++ and work around it via the SDK's copy.
if ! printf '#include <algorithm>\nint main(){return 0;}\n' \
        | clang++ -x c++ -std=c++17 -fsyntax-only - 2>/dev/null; then
  SDK="$(xcrun --show-sdk-path)"
  export CXXFLAGS="-nostdinc++ -isystem $SDK/usr/include/c++/v1 ${CXXFLAGS:-}"
  echo "note: working around broken CommandLineTools libc++ using $SDK"
fi

cmake -B "$BUILD_DIR" -S . -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$(sysctl -n hw.ncpu)"

echo
echo "Built ($BUILD_TYPE)."
echo "  AU:         ~/Library/Audio/Plug-Ins/Components/SC1000.component (auto-installed)"
echo "  Standalone: $BUILD_DIR/SC1000_artefacts/$BUILD_TYPE/Standalone/SC1000.app"
