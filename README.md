# sc1000-controller

Turn an **SC1000 MK2** digital scratch instrument into a **USB-MIDI controller**
for a host VST / Mixxx — crossfader → volume/on-off, jog wheel → playback
speed & direction — with **no soldering** and **without opening the case**.

This is a layer on top of [rasteri/SC1000](https://github.com/rasteri/SC1000)
(GPLv2). It adds MIDI *output* to the firmware (which only does MIDI input today)
and builds a flashable update that deploys over the USB stick.

> Status: **scaffold / pre-first-build.** The firmware code is written; the build
> harness is ready but unproven on hardware. The one open hardware question is
> whether this unit's micro-USB data lines reach the A13's USB0 — the first
> plug-in is the test (see *Open risk* below).

## How it works (Route 1)

The SC1000's micro-USB **power** port is wired (on the SC1000, unlike the SC500)
to the A13's **USB0 / OTG controller**, which can act as a USB *device*. We:

1. build a **kernel** with the USB gadget framework + `g_midi` (legacy USB-MIDI
   gadget), which auto-creates an ALSA rawmidi device at boot;
2. patch the **device tree** so `usb0` runs in `peripheral` mode;
3. modify **xwax** so the input thread emits the crossfader/volume/jog/touch it
   already reads, out to that gadget (`software/sc_midi_out.c`).

The result: plug **one USB-C↔micro-USB cable** from your Mac to the SC1000's
power port → the Mac **powers it** *and* sees it as a **USB-MIDI device**. No
stick, no wall power, no soldering.

Everything ships via the SC1000's existing stick updater (it overwrites
`zImage` + `dtb` + `xwax` on the internal boot partition), so the sealed SD card
is never touched.

## MIDI map

Channel = `midioutchannel` (default 0 → MIDI ch 1). Values are 7-bit.

| Control            | Message                  | Notes                                   |
|--------------------|--------------------------|-----------------------------------------|
| Crossfader in 1    | CC **16**                | 0–127                                   |
| Crossfader in 2    | CC **17**                | 0–127                                   |
| Volume pot 1       | CC **18**                | 0–127                                   |
| Volume pot 2       | CC **19**                | 0–127                                   |
| Jog rotation       | CC **20** (relative)     | two's-complement: 1–63 fwd, 127–65 rev  |
| Jog touch          | Note **20** on/off       | note-on when touched, note-off released |

Tunable in `scsettings.txt` on the stick: `midiout` (0/1), `midioutchannel`,
`midioutrate` (µs between sends, default 1000), `midioutdev` (force a specific
ALSA rawmidi device; empty = auto-detect the gadget).

## Build

Needs Docker Desktop. From the repo root:

```sh
./build/run.sh
```

First run downloads buildroot 2018.08.4 and builds a toolchain + kernel
(~30–60 min; cached in `build/cache/` afterwards). Output: **`build/out/sc.tar`**.

Apple Silicon: if the build misbehaves, `PLATFORM=linux/amd64 ./build/run.sh`.

It uses your sibling `../SC1000` checkout as the firmware source if present,
otherwise clones upstream.

## Flash

1. Copy `build/out/sc.tar` to the **root of a FAT32 USB stick**.
2. Insert it in the SC1000's **USB-A** slot.
3. **Hold a beat button** and power on. It mounts the boot partition, overwrites
   kernel/dtb/xwax, and says **"successful"** (or "failed").
4. Power off, remove the stick.

## Test

1. Connect Mac ↔ SC1000 **micro-USB** with a **USB-C→micro-USB data cable**.
2. Power on the SC1000 (it's powered by the Mac over this cable).
3. On the Mac, open a MIDI monitor (e.g. *Audio MIDI Setup*, or `aseqdump` /
   a tool like MIDI Monitor). A device should appear. Move the crossfader and
   spin the jog → you should see CC 16/17 and CC 20 + Note 20.

## Open risk (and the fallback)

The build runs regardless, but MIDI only flows if this MK2's micro-USB actually
carries USB0's D+/D-. The v0.2 schematic says it does; one community comment said
it doesn't (most likely a misdiagnosis — stock firmware runs no gadget, so
nothing enumerates even when wired). **If no MIDI device shows up**, we fall back
to **Route 2**: a tiny USB hub in the USB-A port holding the stick + a cheap
class-compliant USB-MIDI interface; the same `xwax` MIDI output then goes out
that interface's DIN. No soldering either.

## Revert to factory

```sh
./build/make-stock.sh      # writes build/out/sc-stock.tar from pristine upstream
```
Flash `sc-stock.tar` the same way (hold a beat button) to restore stock firmware.

## Layout

```
firmware/
  overlay/software/sc_midi_out.{c,h}   our MIDI-output module
  sc1000-midi-out.patch                edits to xwax.c/.h, sc_input.c, Makefile
build/
  Dockerfile run.sh build.sh           Docker build of the firmware tarball
  make-stock.sh                        build a factory-restore tarball
  kernel-gadget.fragment               kernel config: USB gadget + g_midi
host/    Mixxx mapping / host glue      (Stage 1.5)
vst/     custom JUCE sampler/scratcher  (Stage 2)
```

## Roadmap

- **Stage 1** — firmware MIDI out + confirm the controller works (this repo).
- **Stage 1.5** — Mixxx MIDI mapping (`engine.scratch*`) to scratch a dropped
  sample with zero VST code, validating feel.
- **Stage 2** — custom JUCE VST: drop a sample, jog scrubs it, fader gates it,
  with a waveform + playhead display (fixes the SC1000's no-visual-feedback gripe).

## License

GPLv2, inherited from xwax / SC1000. See `COPYING`. Upstream © Mark Hills (xwax)
and Andrew Tait / rasteri (SC1000).
