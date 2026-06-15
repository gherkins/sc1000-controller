# scratch-vst

An **AU plugin** (+ Standalone) to **drop in a sample and scratch it** with a
hardware controller — variable-rate, reversible playback driven by the jog wheel
and gated by the crossfader, with a waveform + playhead and spinning platter the
hardware itself lacks.

Built for the **[SC1000](https://github.com/gherkins/sc1000-controller)** running
the USB-MIDI firmware, but works with any controller sending the same MIDI map.

> **Build target: AU, not VST3.** VST3 normalizes incoming MIDI CC into plugin
> parameters, destroying the SC1000's relative jog stream. AU and Standalone pass
> raw CC through.

## Status

MVP builds, scratches a dropped sample, and **passes `auval`**. Not yet tested
against the hardware.

## Build

```sh
./build.sh            # ./build.sh Release for low-latency feel
```

JUCE 8 is pulled locally via CMake FetchContent — nothing installed globally. The
AU auto-installs to `~/Library/Audio/Plug-Ins/Components/`; artifacts land in
`build/ScratchVST_artefacts/`. Test via the Standalone app or in Renoise (set the
instrument's MIDI input to the controller — not MIDI-mapping-to-parameter, which
breaks the relative jog).

## Docs

- [`docs/MIDI-MAPPING.md`](docs/MIDI-MAPPING.md) — device → plugin MIDI contract (**start here**)
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — plugin architecture, scratch DSP, milestones

## License

Intended GPLv2-compatible (the controller side is GPLv2). _Created with Claude Opus 4.8._
