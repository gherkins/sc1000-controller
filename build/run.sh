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

mkdir -p "$PROJ/build/out"

ARGS=()
if [ -n "$PLATFORM" ]; then ARGS+=(--platform "$PLATFORM"); fi

docker build "${ARGS[@]}" -t sc1000-build "$HERE"

# Cache the heavy buildroot build in a Docker NAMED VOLUME (Linux-native ext4),
# NOT a macOS bind mount: the bind mount's case-insensitive / symlink quirks
# break buildroot's toolchain build (uClibc libc.so link fails). The named volume
# is case-sensitive and persists across runs, so after the first build, xwax
# tweaks recompile in seconds. Make the unprivileged build user own it.
docker volume create sc1000-scbuild >/dev/null
docker run --rm --user 0 "${ARGS[@]}" -v sc1000-scbuild:/home/builder/scbuild \
  sc1000-build chown builder:builder /home/builder/scbuild

# Only /work (repo: read sources, write build/out) and /src are bind-mounted.
MOUNTS=(-v "$PROJ":/work -v sc1000-scbuild:/home/builder/scbuild)
if [ -d "$SRC/software" ]; then
  echo "==> Using local SC1000 source: $SRC"
  MOUNTS+=(-v "$(cd "$SRC" && pwd)":/src:ro -e SC1000_SRC=/src)
else
  echo "==> No local SC1000 at $SRC - container will clone upstream"
fi

# Use a TTY only when we actually have one (so this works in CI / background too)
TTY=()
if [ -t 0 ] && [ -t 1 ]; then TTY=(-it); fi

docker run --rm "${TTY[@]}" "${ARGS[@]}" "${MOUNTS[@]}" sc1000-build bash /work/build/build.sh
echo "==> Artifacts: $PROJ/build/out/"
