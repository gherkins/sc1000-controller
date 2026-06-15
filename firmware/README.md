# firmware

Our additions to the SC1000 xwax firmware. Kept as an **overlay + patch** against
upstream [rasteri/SC1000](https://github.com/rasteri/SC1000) so the upstream tree
stays pristine; `build/build.sh` assembles them at build time.

## Contents

- `overlay/software/sc_midi_out.c` / `.h` — new module. Opens the USB-MIDI gadget
  (auto-detected, or `midioutdev` from settings) and emits:
  - crossfader/volume ADCs as CC (only on change),
  - jog rotation as a **relative** CC (two's-complement, chunked so fast
    reversals never drop — the key feel trick),
  - jog touch as note on/off,
  throttled to `midioutrate` µs.
- `sc1000-midi-out.patch` — edits to upstream:
  - `software/Makefile` — add `sc_midi_out.o`
  - `software/xwax.h` — new `SC_SETTINGS` fields (`midiout`, `midioutchannel`,
    `midioutrate`, `midioutdev`)
  - `software/xwax.c` — defaults + `scsettings.txt` parsing for those
  - `software/sc_input.c` — `#include`, `sc_midi_out_init()` once, and
    `sc_midi_out_update(...)` per input-loop iteration

## Regenerating the patch

If you edit the upstream files in a working tree:

```sh
cd /path/to/SC1000
git diff -- software/Makefile software/xwax.c software/xwax.h software/sc_input.c \
  > /path/to/sc1000-controller/firmware/sc1000-midi-out.patch
```

New files (not edits) belong in `overlay/`, not the patch.

## Build

Needs Docker. From the repo root:

```sh
./build/run.sh        # or: make firmware
```

First run downloads buildroot 2018.08.4 and builds a toolchain + custom kernel
(~30–60 min, cached afterwards); it clones upstream `rasteri/SC1000` as the
firmware source (or set `SC1000_SRC=/path/to/SC1000`). Output: **`build/out/stick/`**
(the updater script `xwax` + `sc.tar`).

Revert to stock: `./build/make-stock.sh` (or `make stock`) builds `sc-stock.tar`;
flash it the same way.

## Flash

1. Copy both files from `build/out/stick/` (`xwax` + `sc.tar`) to a **FAT32 USB stick**.
2. Insert it in the SC1000's **USB-A** port; plug headphones into the audio jack.
3. **Hold a beat button** and power on → it says "updated successfully."
4. Power off, remove the stick. Connect the micro-USB to your computer → the unit
   appears as a Core MIDI device (currently named `MIDI Gadget` — a rename to
   "SC1000" is a deferred firmware change, see `vst/docs/ARCHITECTURE.md`).

## Build notes

- `CONFIG_USB_STORAGE` is not in `sunxi_defconfig`; it must be in the kernel config
  (`build/kernel-gadget.fragment`) or the running kernel can't mount the USB stick.
- The DTB drops the `usb0` vbus/id-detect GPIOs (the SC1000 straps VBUS-detect high)
  so the gadget attaches.
- Build on the container's own filesystem, not a macOS bind mount (buildroot's
  toolchain build fails on it); `build/run.sh` uses a Docker named volume.
- The device mounts the stick read-only; the diagnostics in `build/diag/` remount rw.

## Test the MIDI output

```sh
swiftc -O host/midimon.swift -o /tmp/midimon && /tmp/midimon
```

Move the controls to see the CC/Note messages. Any Core MIDI client (the SC1000
plugin, Mixxx, a DAW) sees the same device. Full control → MIDI map:
[`host/sc1000-controls.md`](../host/sc1000-controls.md).
