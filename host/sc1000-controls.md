# SC1000 MK2 — controls → MIDI map

All messages on MIDI channel = `midioutchannel` (default 0 → "channel 1"), from
the **MIDI Gadget** USB device. Set in `scsettings.txt` on the stick.

| Control                | Location | Firmware source            | MIDI out            | Status |
|------------------------|----------|----------------------------|---------------------|--------|
| **Crossfader**         | front    | PIC ADC XFADER1 `ADCs[0]`  | **CC 16** (0–127)   | ✅ MVP |
| Crossfader (2nd chan)  | front    | PIC ADC XFADER2 `ADCs[1]`  | **CC 17** (0–127)   | ✅     |
| Sample volume pot      | back     | PIC ADC POT1 `ADCs[2]`     | **CC 18** (0–127)   | ✅     |
| Beat volume pot        | back     | PIC ADC POT2 `ADCs[3]`     | **CC 19** (0–127)   | ✅     |
| **Jog wheel** rotation | top      | AS5601 encoder             | **CC 20** relative¹ | ✅ MVP |
| Jog touch              | top      | capacitive (PIC bit 4)     | **Note 20** on/off  | ✅     |
| Sample prev / next     | back     | PIC `buttons[0]` / `[1]`   | **Note 21 / 22**    | ✅ (added) |
| Beat prev / next       | back     | PIC `buttons[2]` / `[3]`   | **Note 23 / 24**    | ✅ (added) |
| 4 × cue buttons        | top      | GPIO / MCP23017 (IO map)   | —                   | ⬜ TODO |
| Start button           | front    | GPIO (IO map)              | —                   | ⬜ TODO |
| Shift button           | front    | GPIO (IO map)              | —                   | ⬜ TODO |

¹ Relative jog = two's-complement 7-bit: `01..3F` = forward, `7F..41` = reverse.
Maps directly to Mixxx `engine.scratchTick` or a VST playhead velocity.

## Scope

- **MVP = crossfader + jog wheel** → working over USB-MIDI. ✅
- **Start/stop (nice-to-have):** map any of **Notes 21–24** (the back buttons) to
  start/stop in the host for now. The *dedicated front Start button*, the 4 *cue*
  buttons and *Shift* are read through the device's **GPIO / IO-mapping path**
  (`process_io` + `scsettings.txt` `io=` lines), **not** the PIC, so emitting them
  as MIDI is a follow-up: hook the IO-event path and map the MK2's GPIO pins
  (we can capture which pin is which with a small on-device diagnostic).

## Confirming which Note is which button

Press each back button while watching the monitor (`/tmp/midimon`); note which of
`90 15`/`16`/`17`/`18` (Notes 21–24) each one sends, then label them in the host
mapping. Likewise move the crossfader to see whether it drives CC 16 or CC 17.
