# SC1000 plugin

The macOS scratch instrument — drop in a sample and scratch it with the SC1000 (or
any controller sending the same MIDI map). **AU + Standalone, Apple Silicon.** This
is the developer readme; for the user-facing overview and downloads see the
[top-level README](../README.md).

> **Build target: AU, not VST3.** VST3 normalizes incoming MIDI CC into plugin
> parameters, destroying the SC1000's relative jog stream. AU and Standalone pass
> raw CC through. Don't add VST3 — the core feature can't work through it.

## Build

```sh
./build.sh            # Debug;  ./build.sh Release for low-latency feel
```

JUCE 8 is pulled locally via CMake FetchContent into `build/` — nothing installed
globally. The AU auto-installs to `~/Library/Audio/Plug-Ins/Components/SC1000.component`;
artifacts land in `build/SC1000_artefacts/`. From the repo root you can also use
`make vst`, `make auval`, `make shot`, `make standalone`.

## Validate

- `auval -v aumu Scr1 Scvt` — Apple AU validation (the closest thing to CI).
- The **Standalone** app or **Renoise** — for real MIDI/audio against the hardware
  (set the controller as the instrument's MIDI input, not MIDI-mapping-to-parameter).
- **`ScratchShot`** — `build/ScratchShot_artefacts/<Config>/ScratchShot [out.png] [play]`
  renders the editor to a PNG with no screen/DAW; also a quick smoke test that the
  editor constructs.

## Docs

- [`docs/MIDI-MAPPING.md`](docs/MIDI-MAPPING.md) — device → plugin MIDI contract (**start here**).
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — scratch DSP, the AU-not-VST3 decision, open questions.
- [`../host/sc1000-controls.md`](../host/sc1000-controls.md) — the hardware → MIDI map (producer side).
