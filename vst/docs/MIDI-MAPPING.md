# MIDI mapping — SC1000 → plugin (the contract)

This is the **device → plugin** MIDI contract: exactly what the SC1000 controller
firmware emits, verified live on an SC1000 MK2. The plugin's MIDI-input layer
parses this. Producer side (same repo): the canonical hardware → MIDI map is
[`host/sc1000-controls.md`](../../host/sc1000-controls.md), emitted by
`firmware/overlay/software/sc_midi_out.c`.

## Device identity

- **Class-compliant USB-MIDI device** — no driver needed.
- Core MIDI / OS device name: **`MIDI Gadget`** (USB vendor "Grey Innovation",
  product "MIDI Gadget" — it's the Linux `g_midi` gadget). idVendor `0x17b3`,
  idProduct `0x0004` (don't match on these; match on the name or let the user pick).
- Connection: one USB cable from the SC1000's micro-USB port to the host (that
  cable also powers the unit).

## Channel

All messages are on **MIDI channel 1** (status low-nibble `0`) by default —
configurable on the device via `midioutchannel` in `scsettings.txt`. So default
status bytes: CC `0xB0`, Note-On `0x90`, Note-Off `0x80`. Treat the channel as
configurable; don't hard-filter on channel 1.

## Message map

| # | Control | Message | Range / encoding |
|---|---|---|---|
| CC **16** (`0x10`) | **Crossfader** | Control Change | 0–127 absolute (plugin: double-cut, centre-open — full at 64, silent at 0/127) |
| CC **17** (`0x11`) | Crossfader (2nd ADC channel) | Control Change | 0–127 — **mirrors CC16** (see quirks) |
| CC **18** (`0x12`) | Volume pot A (sample, back) | Control Change | 0–127 absolute |
| CC **19** (`0x13`) | Volume pot B (beat, back) | Control Change | 0–127 absolute |
| CC **20** (`0x14`) | **Jog wheel** | Control Change | **relative**, two's-complement 7-bit (see below) |
| CC **21** (`0x15`) | **Jog touch level** (capacitive) | Control Change | continuous `0`/`127`, re-sent every flush (≥ CC firmware build) |
| Note **20** (`0x14`) | **Jog touch** edge (capacitive) | Note-On / Note-Off | on = touched, off = released (kept for edge-based hosts) |
| Note **21** (`0x15`) | Button: sample-prev (back) | Note-On / Off | vel 127 press, 0 release |
| Note **22** (`0x16`) | Button: sample-next (back) | Note-On / Off | " |
| Note **23** (`0x17`) | Button: beat-prev (back) | Note-On / Off | " |
| Note **24** (`0x18`) | Button: beat-next (back) | Note-On / Off | " |
| Note **25** (`0x19`) | **Shift** (front) | Note-On / Off | **held gate**: on = pressed, off = released |
| Note **26** (`0x1A`) | Start/Stop, deck 0 | Note-On→Off | momentary **tap** — reserved (no physical button on MK2) |
| Note **27** (`0x1B`) | **Start/Stop** (front) | Note-On→Off | momentary **tap** — the MK2's physical button (deck 1) |
| Note **32** (`0x20`) | **Cue** bottom-left  | Note-On→Off | tap → selects **pitch** mode |
| Note **33** (`0x21`) | **Cue** top-left     | Note-On→Off | tap → selects **curve** mode |
| Note **34** (`0x22`) | **Cue** top-right    | Note-On→Off | tap → selects **brake** mode |
| Note **35** (`0x23`) | **Cue** bottom-right | Note-On→Off | tap → selects **volume** mode |

> Note CC20 and Note20 share number `0x14` but are different message **types**
> (CC `0xB0` vs Note `0x90/0x80`) — they don't collide.

### Jog wheel (CC 20) — relative decode

CC20 carries a **signed delta** in two's-complement 7-bit:

```
value 0x01..0x3F  →  +1 .. +63   (forward / clockwise)
value 0x7F..0x41  →  -1 .. -63   (reverse / counter-clockwise)

int delta = (value < 64) ? value : value - 128;   // -63..+63, 0 unused
```

- **Accumulate** deltas to track movement. Deltas larger than ±63 are split into
  several consecutive CC20 messages, so summing is correct.
- Units: the jog sensor (AS5601) is **4096 counts per full platter revolution**.
- Rate: throttled on the device to ~1 kHz (`midioutrate`, default 1000 µs). Each
  message is the movement since the last flush.

### Jog touch (CC 21 level + Note 20 edge)

Two representations of the same capacitive sensor:

- **CC 21** — the **continuous level** (`0` released / `127` touched), re-sent **every
  flush** (~1 kHz). Preferred: the host always has the live state, so a dropped or
  spurious packet self-heals on the next cycle. Use this when present.
- **Note 20** — the **edge** (`90 14 7F` placed / `80 14 00` lifted), sent only on
  change. Kept for edge-based hosts (e.g. Mixxx). Because it's edge-only, a single
  lost/spurious event can leave the host stuck until the next change — which is why
  CC 21 was added.

The device calibrates the capacitive baseline at boot, so touch can occasionally
misfire; CC 21's continuous stream lets the host debounce/smooth it cleanly. The
plugin reads CC 21 when present and falls back to the Note 20 edge otherwise.

### Buttons (Notes 21–24)

- Confirmed order (from `buttons[0..3]`): **21 = sample-prev, 22 = sample-next,
  23 = beat-prev, 24 = beat-next** (the 4 buttons on the back).
- Note-On (vel 127) on press, Note-Off (vel 0) on release. Treat vel-0 Note-On as
  Note-Off too, defensively.

### Shift (Note 25)

- A **held gate**: `90 19 7F` on press, `80 19 00` on release (verified live).
  Unlike the cue/start taps, Shift reports its real held state. The plugin uses it
  as the **crossfader shift-layer gate**: while held, the crossfader stops cutting
  and instead dials the cue-selected parameter (see ARCHITECTURE.md).

### Start/Stop (Notes 26/27) — momentary tap

- Comes out as a **tap**: a Note-On (vel 127) **immediately followed** by a
  Note-Off. The firmware only sees the press edge (no separate release), so treat
  it as a one-shot trigger, **not** a gate — don't wait for a "real" Note-Off.
- The MK2's single front Start/Stop button emits **Note 27** (deck 1, verified).
  Note 26 is the deck-0 counterpart the firmware can emit but the MK2 has no
  physical button for it — reserved.
- In the plugin this is the **only transport toggle** (play/stop); the cue pads no
  longer toggle play — they select the crossfader shift-layer mode.

### Cue buttons (Notes 32–35) — momentary tap

- The 4 top cue buttons each emit a **tap** (Note-On then Note-Off), one note per
  pad. On the MK2 the pads are expander pins 0–3 → notes **32–35**, wired (verified
  live, *not* in cue-number order): **32 = bottom-left, 33 = top-left,
  34 = top-right, 35 = bottom-right.**
- The plugin uses the cue taps to **select the crossfader's shift-layer mode** (not
  transport): BL → pitch, TL → curve, TR → brake, BR → volume (see ARCHITECTURE.md).
  The note→mode pairing is a small lookup (`kCueNoteToMode` in `PluginProcessor.cpp`)
  because the pins aren't in corner order.
- Firmware rule is `32 + <expander pin>`, so other hardware could land elsewhere in
  **32–47** — re-capture with `host/midimon.swift` and adjust the lookup if so.
- Pressing a cue **while Shift is held** emits the **same note** (firmware
  *delete-cue*); the plugin treats cue = mode-select regardless of Shift.

## Quirks to handle

- **CC16 ≈ CC17.** The single physical crossfader drives both ADC channels, so
  both CCs move together. **Use CC16; ignore CC17** (or offer it as an alt).
- **Fader deadband.** The device already suppresses ±1 LSB jitter (only sends on
  ≥2 change, plus the extremes 0/127), so CC16/18/19 are reasonably clean.
- **Jog idle trickle.** When touched/stationary the encoder can emit occasional
  ±1 on CC20. Consider ignoring isolated ±1 deltas, or only scratching while
  touch is active.
- **Touch drops on forward pushes.** Measured: the capacitive pad loses touch for
  ~200–500 ms during the *forward push* of a scratch (CC21 → 0 / Note20-off), while
  holding on the backward pull. It's a real sensor-read dropout, not MIDI loss, and
  there's no host-side fix that doesn't also reintroduce post-release coast drift —
  see ARCHITECTURE.md "Touch sensing — forward-push dropout". The only real fix is at
  the PIC capacitive-sensing level.
- **Bursts.** Fast jog spins produce dense CC20 bursts (coalesced USB-MIDI
  packets). Real Core MIDI clients handle this fine; just accumulate.

## Verifying against hardware

The controller repo has `host/midimon.swift` (a tiny Core MIDI monitor):

```sh
swiftc -O midimon.swift -o /tmp/midimon && /tmp/midimon
```

Move each control and confirm the bytes above. Example capture (jog back/forward,
then sample-prev press/release):

```
MIDI: B0 14 7F        # jog -1
MIDI: B0 14 01        # jog +1
MIDI: B0 10 49        # crossfader = 0x49
MIDI: 90 15 7F        # sample-prev pressed  (Note 21)
MIDI: 80 15 00        # sample-prev released
MIDI: 90 19 7F        # Shift pressed        (Note 25, held)
MIDI: 80 19 00        # Shift released
MIDI: 90 1B 7F        # Start/Stop tap       (Note 27 on...
MIDI: 80 1B 00        #                       ...then off, back-to-back)
MIDI: 90 20 7F        # Cue tap, bottom-left (Note 32 on...
MIDI: 80 20 00        #                       ...then off)
```

(Captured live on an SC1000 MK2: Shift = 25, Start/Stop = 27, cue pads 32–35 =
BL / TL / TR / BR.)
