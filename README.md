# sc1000-controller

Firmware add-on that turns an **[SC1000](https://github.com/rasteri/SC1000)**
portable digital scratch instrument into a **USB-MIDI controller**: the jog wheel,
crossfader, volume pots and buttons stream as MIDI over the micro-USB port. No
soldering — one USB cable to a computer provides power *and* MIDI, and the unit
appears as a Core MIDI device named **`MIDI Gadget`**.

It builds on the SC1000 firmware (a fork of [xwax](https://xwax.org)), which can
*receive* MIDI but not send it; this adds the MIDI output. Verified on an SC1000 MK2.

> ⚠️ **Disclaimer — use entirely at your own risk.** This is experimental,
> unofficial firmware. It rebuilds and flashes your device's kernel, device tree
> and boot partition, and it can **brick, damage, or "fry" your SC1000 / SC500**
> (or anything you connect it to). **You alone are responsible for whatever
> happens to your hardware.** There is **no warranty of any kind** (see GPLv2
> §11–12). Keep the factory backup (`build/make-stock.sh`) and read the recovery
> notes *before* you flash. If you're not comfortable recovering a device that
> won't boot, don't flash it.

## MIDI map

All on MIDI channel 1 (configurable via `midioutchannel`). Full reference:
[`host/sc1000-controls.md`](host/sc1000-controls.md).

| Control | MIDI |
|---|---|
| Crossfader | CC 16 (and CC 17, mirrored) |
| Volume pots (back) | CC 18 / CC 19 |
| Jog wheel | CC 20, relative (two's-complement: `01..3F` fwd, `7F..41` rev) |
| Jog touch | Note 20 on/off |
| Back buttons (sample/beat prev/next) | Notes 21–24 |

## How it works

- The micro-USB power port is wired to the A13 SoC's USB0 (OTG) controller, run as
  a USB device gadget (`g_midi`).
- A custom kernel + device tree enable the gadget (`usb0` set to `peripheral`);
  `sc_midi_out.c` (added to xwax) emits the controls as MIDI.
- Everything deploys through the SC1000's existing USB-stick updater (new
  `zImage` + `dtb` + `xwax`).

## Repository layout

```
firmware/
  overlay/software/sc_midi_out.{c,h}   the MIDI-output module
  sc1000-midi-out.patch                edits to xwax.c/.h, sc_input.c, Makefile
build/
  Dockerfile run.sh build.sh           Dockerised buildroot 2018.08.4 -> sc.tar
  kernel-gadget.fragment               kernel config (gadget + g_midi + USB storage)
  make-stock.sh                        build a factory-restore tarball
  diag/                                on-device USB/gadget diagnostics
host/
  sc1000-controls.md                   full control -> MIDI map
  midimon.swift                        tiny Core MIDI monitor
  sc1000.midi.xml / sc1000-scripts.js  Mixxx mapping
```

## Download (pre-built)

Don't want to build it yourself? Grab the ready-to-flash firmware from the
**[latest release](https://github.com/gherkins/sc1000-controller/releases/latest)**
— download and unzip `sc1000-controller-firmware-v*.zip` to get `xwax` + `sc.tar`,
then jump to [Flash](#flash). SHA-256 checksums are in the release notes.

## Build

Needs Docker. From the repo root:

```sh
./build/run.sh
```

First run downloads buildroot 2018.08.4 and builds a toolchain + kernel (~30–60
min, cached afterwards); it clones upstream `rasteri/SC1000` as the firmware
source (or set `SC1000_SRC=/path/to/SC1000`). Output: **`build/out/stick/`** (the
updater script + `sc.tar`).

## Flash

1. Copy both files from `build/out/stick/` (`xwax` + `sc.tar`) to a **FAT32 USB stick**.
2. Insert it in the SC1000's **USB-A** port; plug headphones into the audio jack.
3. **Hold a beat button** and power on → it says "updated successfully."
4. Power off, remove the stick. Connect the micro-USB to your computer → `MIDI Gadget` appears.

Revert to stock: `./build/make-stock.sh` builds `sc-stock.tar`; flash it the same way.

## Build notes

- `CONFIG_USB_STORAGE` is not in `sunxi_defconfig`; it must be in the kernel config
  or the running kernel can't mount the USB stick.
- The DTB drops the `usb0` vbus/id-detect GPIOs (the SC1000 straps VBUS-detect
  high) so the gadget attaches.
- Build on the container's own filesystem, not a macOS bind mount (buildroot's
  toolchain build fails on it); `run.sh` uses a Docker named volume.
- The device mounts the stick read-only; the diagnostics in `build/diag/` remount rw.

## Test

```sh
swiftc -O host/midimon.swift -o /tmp/midimon && /tmp/midimon
```

Move the controls to see the CC/Note messages. Any Core MIDI client (Mixxx, a
DAW, a VST) sees the same device.

## Credits & license

_AI-transparency: created with Claude Opus 4.8._

GPLv2 — inherited from xwax (© Mark Hills) and SC1000 (© Andrew Tait / rasteri).
See [`COPYING`](COPYING). This is an independent add-on; the build pulls the SC1000
firmware from upstream rather than vendoring it.
