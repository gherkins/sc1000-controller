#!/bin/bash
# Build sc.tar inside Docker (run this from your Mac).
#
#   ./build/run.sh
#
# Apple Silicon: if the buildroot host-build misbehaves, force x86 emulation:
#   PLATFORM=linux/amd64 ./build/run.sh
#
# Uses your local ../SC1000 reference clone as the firmware source if present,
# otherwise the container clones upstream rasteri/SC1000.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"     # build/
PROJ="$(cd "$HERE/.." && pwd)"            # repo root
SRC="${SC1000_SRC:-$PROJ/../SC1000}"      # local SC1000 reference (sibling)
PLATFORM="${PLATFORM:-}"

mkdir -p "$PROJ/build/out" "$PROJ/build/cache"

ARGS=()
[ -n "$PLATFORM" ] && ARGS+=(--platform "$PLATFORM")

docker build "${ARGS[@]}" -t sc1000-build "$HERE"

MOUNTS=(-v "$PROJ":/work -v "$PROJ/build/cache":/home/builder/scbuild)
if [ -d "$SRC/software" ]; then
  echo "==> Using local SC1000 source: $SRC"
  MOUNTS+=(-v "$(cd "$SRC" && pwd)":/src:ro -e SC1000_SRC=/src)
else
  echo "==> No local SC1000 at $SRC - container will clone upstream"
fi

docker run --rm -it "${ARGS[@]}" "${MOUNTS[@]}" sc1000-build bash /work/build/build.sh
echo "==> Artifacts: $PROJ/build/out/"
