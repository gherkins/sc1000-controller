# host (Stage 1.5)

Host-side glue to drive software from the SC1000's MIDI output — the fast way to
validate feel before writing the VST.

**Mixxx** is the quickest path: it has first-class MIDI scratch support
(`engine.scratchEnable` / `engine.scratchTick`) plus a sampler, so a mapping can
scratch a dropped sample with zero custom code.

Planned here:
- `sc1000.midi.xml` + `sc1000-scripts.js` — a Mixxx controller mapping:
  - CC 20 (jog relative) → `engine.scratchTick`
  - Note 20 (jog touch) → `engine.scratchEnable` / `scratchDisable`
  - CC 16/17 (crossfader) → deck gain / crossfader
  - CC 18/19 (pots) → volumes

See the MIDI map in the top-level `README.md`.
