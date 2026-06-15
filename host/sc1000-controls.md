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
| Jog touch (level)      | top      | capacitive (PIC bit 4)     | **CC 21** 0/127 cont.⁴ | ✅     |
| Jog touch (edge)       | top      | capacitive (PIC bit 4)     | **Note 20** on/off  | ✅     |
| Sample prev / next     | back     | PIC `buttons[0]` / `[1]`   | **Note 21 / 22**    | ✅ (added) |
| Beat prev / next       | back     | PIC `buttons[2]` / `[3]`   | **Note 23 / 24**    | ✅ (added) |
| Shift button           | front    | IOevent `ACTION_SHIFTON/OFF` | **Note 25** on/off² | ✅ (added) |
| Start/Stop button      | front    | IOevent `ACTION_STARTSTOP` | **Note 26 / 27** tap³ | ✅ (added) |
| 4 × cue buttons        | top      | IOevent `ACTION_CUE` (expander pin) | **Note 32 + pin** tap³ | ✅ (added) |

¹ Relative jog = two's-complement 7-bit: `01..3F` = forward, `7F..41` = reverse.
Maps directly to Mixxx `engine.scratchTick` or a VST playhead velocity.

² **Shift** is a held gate: note-**on** (vel 127) when pressed, note-**off** when
released — so the host can read the shift state and implement its own shift layer.

³ **Cue** and **Start/Stop** only get a *press* edge from the firmware (no
release), so they go out as a momentary **tap**: a note-on (vel 127) immediately
followed by a note-off. Start/Stop is `26` for deck 0 and `27` for deck 1. Each
cue button's note is `32 + <expander pin>`, so the four corners land on four
**stable but pin-dependent** notes somewhere in `32..47` (identify them by ear/eye
— see below). Pressing a cue **while Shift is held** fires the firmware's
*delete-cue* action on the same pin, which emits the **same** note; the host
distinguishes "set" vs "delete" from the separately-reported Shift state.

⁴ **Jog touch** is emitted two ways: **CC 21** carries the continuous level
(`0`/`127`) re-sent every flush, so the host always has the live state and a dropped
or spurious packet self-heals next cycle — prefer this. **Note 20** still sends the
on/off edge for edge-based hosts (Mixxx). The edge alone can leave a host stuck if an
event is lost, which is why the continuous CC was added.

## Scope

- **MVP = crossfader + jog wheel** → working over USB-MIDI. ✅
- **All buttons now emit MIDI.** The front *Shift* / *Start-Stop* and the 4 top
  *cue* buttons are read through the device's **IO-mapping path** (`process_io` →
  `IOevent`, driven by the `gpio=` lines in `scsettings.txt`), **not** the PIC.
  Rather than reading raw GPIO pins, `sc_midi_out_io_event()` hooks `IOevent` and
  emits MIDI keyed on the firmware *action* (CUE / SHIFTON / SHIFTOFF / STARTSTOP),
  so it inherits the existing debounce + shift handling and works regardless of
  the exact MK2 pin wiring. MIDI-driven maps are ignored so host MIDI never echoes
  back out.

## Confirming which Note is which button

Press each back button while watching the monitor (`/tmp/midimon`); note which of
`90 15`/`16`/`17`/`18` (Notes 21–24) each one sends, then label them in the host
mapping. Likewise move the crossfader to see whether it drives CC 16 or CC 17.

For the new IO buttons: press **Shift** and confirm `90 19` (Note 25) on press /
`80 19` on release; tap **Start/Stop** for `90 1A`/`1B` (Note 26/27); then tap
each of the **4 cue buttons** in turn and record which note in `90 20..2F`
(Notes 32–47) each corner sends, so you can label the corners in the host map.
