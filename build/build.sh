#!/bin/bash
# Build the SC1000 "controller" firmware update tarball (sc.tar):
#   - custom kernel  (USB gadget + g_midi)         -> zImage
#   - patched DTB    (usb0 OTG = peripheral)        -> sun5i-a13-olinuxino.dtb
#   - modified xwax  (emits fader/jog/touch as MIDI)-> xwax
# packaged the way the on-device updater expects.
#
# Usually invoked inside Docker via build/run.sh, but also works standalone on
# Linux. Override paths with env vars: SC1000_SRC, OUT, SCRATCH.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BRVER=2018.08.4

SRC="${SC1000_SRC:-/src}"            # upstream SC1000 source tree
OUT="${OUT:-$HERE/out}"              # where sc.tar lands (host-visible)
SCRATCH="${SCRATCH:-$HOME/scbuild}"  # heavy build dir (container-local / cached)
WORK="$SCRATCH/work"                 # assembled firmware source
BR="$SCRATCH/buildroot-$BRVER"

mkdir -p "$OUT" "$SCRATCH"

# --- 0. Get the SC1000 source -------------------------------------------------
if [ ! -d "$SRC/software" ]; then
  echo "==> No SC1000 source at $SRC; cloning upstream rasteri/SC1000"
  rm -rf "$SCRATCH/SC1000"
  git clone --depth 1 https://github.com/rasteri/SC1000.git "$SCRATCH/SC1000"
  SRC="$SCRATCH/SC1000"
fi

# --- 1. Assemble firmware source = upstream + our overlay + our patch ----------
echo "==> Assembling firmware source in $WORK"
rm -rf "$WORK"; mkdir -p "$WORK"
rsync -a --exclude '.git' "$SRC"/ "$WORK"/
cp -v "$HERE"/../firmware/overlay/software/* "$WORK"/software/
# upstream mixes LF/CRLF; normalise the patch targets + the patch to LF so it
# applies deterministically (line endings don't affect compilation)
sed -i 's/\r$//' "$WORK"/software/Makefile "$WORK"/software/xwax.c "$WORK"/software/xwax.h "$WORK"/software/sc_input.c
sed 's/\r$//' "$HERE/../firmware/sc1000-midi-out.patch" > "$WORK/.midi-out.patch"
( cd "$WORK" && patch -p1 < .midi-out.patch )

# version stub (the assembled tree has no .git for mkversion to query)
printf '#!/bin/sh\necho sc1000-controller > .version 2>/dev/null || true\necho sc1000-controller\n' \
    > "$WORK/software/mkversion"
chmod +x "$WORK/software/mkversion"

# --- 2. Buildroot: toolchain + alsa-lib + kernel ------------------------------
if [ ! -d "$BR" ]; then
  echo "==> Downloading buildroot $BRVER"
  wget -O "$SCRATCH/br.tar.gz" "https://buildroot.org/downloads/buildroot-$BRVER.tar.gz"
  tar -C "$SCRATCH" -xf "$SCRATCH/br.tar.gz"
fi
cp "$WORK/os/buildroot/buildroot_config" "$BR/.config"
# point the kernel build at our gadget config fragment
sed -i "s|^BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=.*|BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES=\"$HERE/kernel-gadget.fragment\"|" "$BR/.config"
make -C "$BR" olddefconfig
echo "==> Building toolchain + alsa-lib + kernel (first run is slow, ~30-60 min)"
make -C "$BR" toolchain alsa-lib linux

# --- 3. Cross-compile our xwax ------------------------------------------------
CC="$(ls "$BR"/output/host/bin/*-linux-*-gcc 2>/dev/null | head -1 || true)"
[ -z "${CC:-}" ] && CC="$(ls "$BR"/output/host/usr/bin/*-linux-*-gcc 2>/dev/null | head -1 || true)"
[ -z "${CC:-}" ] && { echo "!! could not find cross gcc under $BR/output/host"; exit 1; }
echo "==> CC=$CC"
make -C "$WORK/software" clean || true
make -C "$WORK/software" CC="$CC" SDL_LIBS="-lpthread -liconv" ALSA_LIBS="-lasound"

# --- 4. Patch the device tree: usb0 (OTG) -> peripheral -----------------------
DTC="$(find "$BR/output/host" -name dtc -type f | head -1)"
DTB_IN="$(find "$BR/output" -name sun5i-a13-olinuxino.dtb | head -1)"
echo "==> Patching DTB: $DTB_IN"
"$DTC" -I dtb -O dts "$DTB_IN" -o "$SCRATCH/sc.dts" 2>/dev/null
if grep -q 'dr_mode = "otg"' "$SCRATCH/sc.dts"; then
  sed -i 's/dr_mode = "otg"/dr_mode = "peripheral"/' "$SCRATCH/sc.dts"
elif grep -q 'dr_mode = "host"' "$SCRATCH/sc.dts"; then
  sed -i 's/dr_mode = "host"/dr_mode = "peripheral"/' "$SCRATCH/sc.dts"
else
  echo "!! WARNING: no dr_mode found to flip - inspect $SCRATCH/sc.dts (node usb@1c13000)"
fi
"$DTC" -I dts -O dtb "$SCRATCH/sc.dts" -o "$SCRATCH/sun5i-a13-olinuxino.dtb" 2>/dev/null

# --- 5. Package sc.tar (layout the on-device updater expects) -----------------
STAGE="$(mktemp -d)"
cp "$SCRATCH/sun5i-a13-olinuxino.dtb" "$STAGE/sun5i-a13-olinuxino.dtb"
cp "$(find "$BR/output" -name zImage | head -1)" "$STAGE/zImage"
cp "$WORK/software/xwax" "$STAGE/xwax"
for f in os-version.mp3 scratchsentence.mp3 successful.mp3 failed.mp3 scsettings.txt; do
  cp "$WORK/updater/tarball/$f" "$STAGE/$f"
done
( cd "$STAGE" && tar -cf "$OUT/sc.tar" * )
rm -rf "$STAGE"

echo
echo "==> DONE: $OUT/sc.tar"
echo "    Copy it to a FAT32 USB stick, insert it, hold a beat button, power on."
ls -la "$OUT/sc.tar"
