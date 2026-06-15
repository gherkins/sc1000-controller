#!/usr/bin/env bash
# Ship a change end-to-end: build → commit → push → install the AU.
#
# Builds Release first (so we never publish code that doesn't compile and so the
# installed .component is the latest version), then commits, pushes to the tracked
# upstream, and copies the freshly built AU into the system Components dir.
#
# Usage: ./ship.sh "commit message" [Debug|Release]   (default build: Release)
set -euo pipefail
cd "$(dirname "$0")"

MSG="${1:-}"
if [[ -z "$MSG" ]]; then
  echo "usage: ./ship.sh \"commit message\" [Debug|Release]" >&2
  exit 1
fi
BUILD_TYPE="${2:-Release}"

# 1. Build — verifies compilation before anything is published.
./build.sh "$BUILD_TYPE"

# 2. Commit (skip cleanly if the tree is already clean).
git add -A
if git diff --cached --quiet; then
  echo "note: nothing to commit — skipping commit/push."
else
  git commit -m "$MSG"
  # 3. Push to the upstream main tracks.
  git push
fi

# 4. Install the freshly built AU into the system Components dir.
#    (CMake's COPY_PLUGIN_AFTER_BUILD already does this on build; this makes the
#    install explicit and authoritative regardless of that setting.)
SRC="build/ScratchVST_artefacts/$BUILD_TYPE/AU/Scratch VST.component"
DST="$HOME/Library/Audio/Plug-Ins/Components"
if [[ ! -d "$SRC" ]]; then
  echo "error: built AU not found at $SRC" >&2
  exit 1
fi
rm -rf "$DST/Scratch VST.component"
cp -R "$SRC" "$DST/"

echo
echo "Shipped ($BUILD_TYPE)."
echo "  installed: $DST/Scratch VST.component"
