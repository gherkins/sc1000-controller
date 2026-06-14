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
