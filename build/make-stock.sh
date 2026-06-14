#!/bin/bash
# Build a "stock" sc.tar from the pristine SC1000 updater/tarball, so you can
# always flash the device back to factory firmware. Runs on the Mac (just tars).
#
#   ./build/make-stock.sh            # uses ../SC1000 by default
#   SC1000_SRC=/path/to/SC1000 ./build/make-stock.sh
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="${SC1000_SRC:-$HERE/../../SC1000}"
OUT="$HERE/out"
mkdir -p "$OUT"

if [ ! -d "$SRC/updater/tarball" ]; then
  echo "SC1000 source not found at $SRC (set SC1000_SRC=/path/to/SC1000)"; exit 1
fi

( cd "$SRC/updater/tarball" && tar -cf "$OUT/sc-stock.tar" * )
echo "Wrote $OUT/sc-stock.tar  -> flash this (hold a beat button) to restore factory firmware."
