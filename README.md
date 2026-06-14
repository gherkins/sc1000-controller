# sc1000-controller

Turn an **[SC1000](https://github.com/rasteri/SC1000)** (rasteri's open-source
portable digital scratch instrument) into a **USB-MIDI controller** — over a
single cable, **no soldering**, and (after a one-time setup) **without opening the
case**.

> **Status: working ✅** — verified live on an **SC1000 MK2 (Acrylic Edition)**.
> Plug a USB-C↔micro-USB cable into a Mac and the unit appears as a Core MIDI
> device named **`MIDI Gadget`**, streaming the crossfader, jog wheel, volume
> pots, jog-touch and the back buttons as MIDI.

This is an **add-on layer** on top of the SC1000 firmware (which is a fork of
[xwax](https://xwax.org)). The stock firmware *receives* MIDI (e.g. Dicer
controllers); this adds the missing piece — making the SC1000 *be* a MIDI
controller, so you can drive a DJ app / VST with the jog and fader. rasteri has
said this is a feature he wants but hasn't built
([#19](https://github.com/rasteri/SC1000/issues/19),
[#30](https://github.com/rasteri/SC1000/issues/30)).

## MIDI map

All on MIDI channel `midioutchannel` (default 0). See
[`host/sc1000-controls.md`](host/sc1000-controls.md) for the full reference.

| Control | MIDI out | |
|---|---|---|
| Crossfader | **CC 16** (and CC 17 — both ADC channels) | ✅ |
| Sample / beat volume pots (back) | **CC 18 / CC 19** | ✅ |
| Jog wheel rotation | **CC 20**, relative two's-complement (`01..3F` fwd, `7F..41` rev) | ✅ |
| Jog touch (capacitive) | **Note 20** on/off | ✅ |
| Back buttons (sample/beat prev/next) | **Notes 21–24** on/off | ✅ |
| Front Start / Shift, top cue buttons | — | ⬜ TODO (GPIO/IO path) |

## How it works (the "Route 1" approach)

The SC1000 runs Linux on an Allwinner **A13** (Olimex A13-SOM). Its **micro-USB
power port is wired to the A13's USB0 OTG controller**, which can act as a USB
*device*. So:

1. **Kernel** — build with the USB gadget framework + the legacy **`g_midi`**
   function (`CONFIG_USB_MIDI_GADGET`), which auto-creates an ALSA rawmidi
   device. (Also `CONFIG_USB_STORAGE`, which mainline `sunxi_defconfig` omits —
   without it the kernel can't even mount the USB stick.)
2. **Device tree** — set `usb0` `dr_mode = "peripheral"` **and remove the
   `usb0_id_det` / `usb0_vbus_det` GPIOs**. The OLinuXino DTB expects GPIO
   VBUS/ID detection that the SC1000 doesn't wire (it straps VBUS-detect high),
   so with the GPIOs present the gadget stays `not attached`; removing them makes
   the PHY assume VBUS is present and the gadget attaches when you plug into a host.
3. **xwax** — the input thread already reads the crossfader, pots, jog angle and
   touch; [`sc_midi_out.c`](firmware/overlay/software/sc_midi_out.c) emits them as
   MIDI to the gadget (jog as **relative** deltas, a small deadband on the faders).

Result: one micro-USB cable to the host = power **and** a class-compliant USB-MIDI
device. No host driver needed.

## Repository layout

```
firmware/
  overlay/software/sc_midi_out.{c,h}   the MIDI-output module (new)
  sc1000-midi-out.patch                edits to xwax.c/.h, sc_input.c, Makefile
build/
  Dockerfile run.sh build.sh           Dockerised buildroot 2018.08.4 -> sc.tar
  kernel-gadget.fragment               kernel config (gadget + g_midi + USB storage)
  make-stock.sh                        build a factory-restore tarball
  diag/                                on-device USB/gadget diagnostics (no shell needed)
host/
  sc1000-controls.md                   full control -> MIDI map
  midimon.swift                        tiny Core MIDI monitor (swiftc -> /tmp/midimon)
  sc1000.midi.xml / sc1000-scripts.js  Mixxx mapping (WIP)
vst/                                   custom JUCE sampler/scratcher (planned)
```

## Build

Needs Docker. From the repo root:

```sh
./build/run.sh
```

First run downloads buildroot 2018.08.4 and builds a cross-toolchain + kernel
(~30–60 min, cached afterwards). It clones upstream `rasteri/SC1000` as the
firmware source (or set `SC1000_SRC=/path/to/SC1000`). Output:
**`build/out/stick/`** (the updater script + `sc.tar`).

On Apple Silicon use native arm64 (default). Build it on the container's own
filesystem — **not** a macOS bind mount (buildroot's toolchain build breaks on the
case-insensitive/symlinky shared mount); `run.sh` handles this with a Docker
named volume.

## Flash

The SC1000's own stick-updater writes a new kernel/dtb/xwax to the internal boot
partition:

1. Copy **both files from `build/out/stick/`** (`xwax` + `sc.tar`) to the root of
   a **FAT32 USB stick**.
2. Insert it in the SC1000's **USB-A** port, plug headphones into the audio jack.
3. **Hold a beat button** and power on → it says **"updated successfully."**
4. Power off, remove the stick.

Then connect the **micro-USB to your computer** — it enumerates as `MIDI Gadget`.

**Revert to stock:** `./build/make-stock.sh` builds `sc-stock.tar` from pristine
upstream; flash it the same way.

## Test

```sh
swiftc -O host/midimon.swift -o /tmp/midimon && /tmp/midimon
```
Move the controls; you'll see the CC/Note messages. (Any app that reads Core MIDI
— Mixxx, a DAW, a VST — sees the same device.)

## ⚠️ Gotchas learned the hard way

If you build this yourself, these will save you days:

- **`CONFIG_USB_STORAGE` is not in `sunxi_defconfig`.** Without it the running
  kernel can't mount the USB stick → no samples, no stick-updates, **and it bricks
  the stick-update path** (the updater can't run). It must be in the kernel config.
  *(If you flash a kernel lacking it, recovery means pulling the microSD and
  writing a good `zImage`+`dtb` onto its FAT boot partition directly.)*
- **The gadget binds but stays `not attached`** until you drop the `usb0` VBUS/ID
  detect GPIOs from the DTB (see "How it works").
- **Build off the macOS bind mount**, or buildroot's uClibc link fails
  (`__fini_array_end isn't defined`).
- The device usually mounts the stick **read-only**; for on-device logging,
  `mount -o remount,rw` first (see `build/diag/`).

## Roadmap

- [ ] Mixxx mapping finalised (`engine.scratch*`) — scratch a dropped sample
- [ ] Custom JUCE VST: drop a sample, jog scrubs it, fader gates, waveform+playhead
- [ ] Front Start/Shift + top cue buttons → MIDI (GPIO/IO-mapping path)
- [ ] Upstream PR to rasteri/SC1000 (additive, behind a setting)

## Credits & license

GPLv2 — inherited from **xwax** (© Mark Hills) and **SC1000** (© Andrew Tait /
rasteri). See [`COPYING`](COPYING). This repo is an independent add-on; the build
pulls the SC1000 firmware from upstream rather than vendoring it.
