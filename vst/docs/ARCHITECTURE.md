# Architecture & open questions

Design notes for the plugin. An **MVP is now built** (`src/`, builds AU +
Standalone, passes `auval`) — the signal-flow sketch below matches the code. The
"Open questions" section still holds the decisions not yet settled; several are
marked with the provisional answer the MVP ships.

## Status — what the MVP implements

- **MIDI decode** (`PluginProcessor.cpp`): CC16 crossfader, CC18/19 volumes,
  CC20 relative jog, Note20 touch — from raw bytes, channel-agnostic. **Transport:
  the Start/Stop tap (Note 27) toggles play/stop.** The **4 cue pads (Notes 32–35)
  select the crossfader's shift-layer mode** and **Shift (Note 25)** gates that
  layer (see "Crossfader shift layer" below). Back buttons 21–24 reserved.
- **Scratch engine** (`ScratchEngine.h`): reversible, variable-rate cubic-
  interpolated playback; jog→time via `kPlatterSpeed = 2275`. **Motor model ported
  from the SC1000 firmware** (`player.c`): **touch the platter → jog drives the
  playhead directly, motor paused** (you hold/scratch the record); **release →
  platter slips back to the motor** (slipmat inertia, `kSlipTau`); **playing →
  spins at 1×**; **stop → motor brakes 1×→0 over ~0.6 s = tape stop** (`kBrakeSpeed`).
  Transport toggled by the cue / Start buttons. A DC blocker keeps a stationary
  platter silent + de-clicks. Idle-trickle ±1 suppressed. Ends **loop** (wrap both
  directions, seamless cubic across the boundary — q10 decided; toggle via
  `ScratchEngine::setLoop`, default on). One voice. *(Touch is gated through
  `TouchGate.h` — ported from the firmware's `capIsTouched || motor_speed == 0`
  decision; see "Touch sensing" below.)*
- **Crossfader** (open q4, decided) = firmware-ported **double-cut (centre-open)** —
  full at the centre, silent at **both** edges, with hysteresis + ~20 ms de-click,
  *not* a linear fade. This is the SC1000 "battle" fader: you cut toward either end
  rather than mixing between two decks. Driven by distance from centre
  (`c = 1 − 2·|fpos − 0.5|`) through the same hysteresis/curve/decay; the **volume
  pot CC18** sets the level. Side benefit: immune to the CC16 idle-drift. The cut
  **curve** (sharp↔soft) is adjustable via the shift layer — sharp = full across
  nearly all travel (hard cut only at the very edges), soft = a gradual tent from
  the centre outward. volB spare (open q5).
- **Self-contained saving**: the loaded sample is embedded in the plugin state
  chunk — confirmed approach (see below).
- **GUI** (`PluginEditor.*`): waveform+playhead, spinning platter w/ DJ marker,
  drag-and-drop + file-open loading.

**Not yet done / next:** hardware feel-tuning of the new shift-layer ranges and the
scratch model; back-button (21–24) assignments; latency tuning. Tuning knobs live in
`ScratchEngine.h` (scratch/cut/brake feel): `kPlatterSpeed` (scrub speed),
`kBrakeSpeed` (tape-stop length), `kSlipTau` (slipmat catch-up), `kFaderOpenPt`/
`kFaderClosePt` (cut point), `kFaderDecay` (cut de-click), `kCurveSharpKnee` (curve
sharpness), and the jog direction (CC20 sign); the **shift-layer fader→param
mappings** (`kPitchRange`, `kVolDetent`, `kBrakeMin/Max`, `kCueNoteToMode`) live in
`PluginProcessor.cpp`.

## Crossfader shift layer (cue-selected modes)

Hold **Shift (Note 25)** and the crossfader stops cutting and instead dials a
parameter; the **cue pads** pick which (default = pitch). Release Shift → it's the
scratch CUT again, value retained. Only **Start/Stop (Note 27)** toggles transport
now. All routing is on the MIDI thread (`PluginProcessor.cpp`); the engine just
reads the resulting scalars from `ControlState`, and volume mode reuses the existing
`volA` gain.

| Cue (corner · note) | Mode | Crossfader does |
|---|---|---|
| bottom-left · 32  | **pitch**  | ±20% varispeed; snaps to unity across centre ±20% |
| bottom-right · 35 | **volume** | 0→unity, detent at ~75% (writes `volA`) |
| top-left · 33     | **curve**  | centre→edge falloff: left = sharp/hard-cut → right = soft tent (Vestax-05 style) |
| top-right · 34    | **brake**  | tape-stop length, short→long (centre = stock) |

The cue note→mode pairing is **hardware-verified** (the MK2's expander pins aren't
in cue order — see MIDI-MAPPING.md) via the `kCueNoteToMode` lookup. The dialed
pitch/curve/brake persist in the plugin state (**v3**).

**UI:** the armed cue pad gets a mode-coloured border; the fader head is neutral
(orange) at rest and takes the active mode's colour only while Shift is held; a
bottom-right panel lists all four values, with a caret (▸) marking the active mode
in its colour.

## Signal flow (sketch)

```
USB-MIDI "MIDI Gadget"
        │  (see MIDI-MAPPING.md)
        ▼
  MIDI input  ──► ControlState (lock-free, shared)
                    • crossfader 0..1        (CC16)
                    • volumes 0..1           (CC18/19)
                    • jogDelta accumulator   (CC20, relative)
                    • touched bool           (Note20)
                    • button events          (Notes21-24)
        ┌───────────────────────────────────────────┐
        ▼ (audio thread)                              ▼ (gui thread)
  ScratchEngine                                   Waveform + playhead
    • playhead (samples, fractional)
    • reads jogDelta + touched each block
    • variable-rate, reversible resample of the
      loaded sample buffer (cubic interpolation)
    • apply crossfader gain + volume
        ▼
   audio out (stereo)
```

The proven algorithm already exists in the SC1000 firmware —
[`software/player.c` `build_pcm()`](https://github.com/rasteri/SC1000) does
cubic-interpolated variable-rate playback with `target_position` driven by the
jog. Worth reading before implementing the engine.

## Jog → time mapping (the core of the feel)

The jog arrives as **relative counts** (CC20). The SC1000 maps counts→audio-time
with `platterspeed` (counts per audio-second; default **2275** ≈ 33 rpm feel;
4096 counts = one platter revolution). So:

```
playheadSeconds += jogDeltaCounts / PLATTER_SPEED;   // PLATTER_SPEED ≈ 2275, tunable
```

Matching this gives the same feel as the hardware. Reverse comes for free (negative
deltas). The audio engine resamples between the previous and new playhead each
block; rate (and direction) = (Δplayhead / blockDuration).

## Touch sensing — the gate (`TouchGate.h`)

The capacitive pad is a single noisy bit straight from the PIC (`sc_input.c`:
`capIsTouched = (result >> 4 & 0x01)`, shipped as CC21 + Note20). There is **no
sensitivity / threshold / hysteresis in the Linux firmware** to tune — that logic
lives in the PIC microcontroller (separate, not in either repo). It is **not** usable
as the sole scratch gate, and a 107 s capture (`make trace`, analysed by
`trace_replay`) said exactly how badly:

```
cap-on rate:   backward pull 91%    forward push 58%    (the bit drops on the push)
66 forward-push dropouts mid-scratch, median 203 ms, up to ~1.16 s,
   with the jog CLEARLY still moving the whole time
only 4 genuine let-gos — and on a let-go the light platter settles
   (jog → 0) within ~21 ms
```

So gating on the cap bit handed control back to the motor in the middle of 58
scratches in that capture ("it keeps running while I scratch"). The split is
**directional**: the cap is unreliable on a forward **push** (it drops, ~58 % on) but
reliable on a backward **pull** (~91 % on). And on a real let-go the platter stops almost
instantly, so the "coast-fly" an earlier design feared (and used to justify *not*
bridging) barely exists. The model below trusts the cap *while it's on* (which covers
pulls and the body of any scratch) and treats the forward-push drop as the residual
hardware limit — see the refinement note.

**The "stick slipmat" model — a finger on the platter is in control; a real lift snaps to
the motor.** While the cap reads **on**, the jog drives the playhead (moving ⇒ scratch,
still ⇒ the record eases to a stop *under* your finger); the moment you **lift** (cap → 0)
it slips *stiffly* to the motor. It does **not** ride the platter's leftover momentum: the
light platter keeps creeping after you let go, so honouring it reads as a sluggish drag /
dead stillstand before the motor catches. Decision (user call): give up the "push it
forward and let it ride" trick; make **release reliably mean "play the sample."** So
`TouchGate` (JUCE-free, unit-tested in `test/touchgate_test.cpp` [`make touchtest`])
follows the jog when:

- **motor stopped** — a dead record you cue by hand (so "not running recognises touch
  without touching" is correct by design), or
- **cap on** — your finger is on the platter (moving = scratch, still = hold/stop).

Only a real **lift** (motor running **and** cap off) **slips to the motor**, *stiffly*
(`kSlipTau` ≈ 12 ms, a "sticky" catch that snaps to 1× rather than coasting). A short hold
(**Coast**, `kTouchReleaseHold` 20 ms) rides a brief cap-off **flicker** (a blip vs a real
lift) before committing to the slip.

**Why trust the cap bit — refinement (2026-06).** An earlier version *also* required the
platter to be **moving** (`cap on AND moving`), to dodge the "cap lingers after lift" fear
above. But the captures prove the cap is reliable exactly where it matters: it sits **on
through the whole of a backward pull** (cap-on ≈ 91 %, vs ≈ 58 % on a forward push) and
drops **cleanly to 0 on a real lift** (settles in ~21 ms). The extra `&& moving` instead
misfired at every **slow point** of a scratch — a turnaround, a slow pull — where the jog
momentarily reads 0 (the ±1 idle deadband, `kJogDeadband`). With the cap still on, the
gate handed to the motor and yanked the playhead toward **+1× against the stroke**: the
*"pulling back fights the motor"* feel the user reported. Replaying `trace.csv` through
both gates: **15 cap-on motor-takeovers → 0**, while genuine-lift latency stays ~16→21 ms
(one block — still snappy). Dropping the qualifier is *surgical*: the only blocks it
changes are "cap on + platter still + motor running", which flip from "release to the
motor" to "hold the record" — physically what touching a playing platter should do.

The remaining forward-push **dropouts** (cap off mid-push, up to ~1.4 s) are a **hardware**
limit (PIC cap-sense) the host can't fix — but the motor they fall back to is +1×, the
*same* direction as a push, so they're barely audible. **Tape-stop** is still available as
a *slow-down* (cap on + decelerating jog → follows it down to a held stop).

**Tradeoff.** Trusting `cap on` means: if a unit's cap genuinely *lingers* on long after a
lift (not seen on this MK2 — lifts read clean), the record would hold **silent** under the
phantom touch instead of resuming the motor. The bounded fix, if it ever shows up, is a
`kHeldMax` timeout — after the cap has been on but the platter dead-still for > ~0.5 s with
the motor running, assume a stuck cap and slip to the motor. **Not added now:** it would
also cut off a deliberate long hold, and the capture shows no lingering. See "Open
questions".

Validated on captures: the cap-on motor-takeover ("fighting the motor") gone, release
still snaps without riding momentum, slow/baby scratching and turnarounds follow cleanly.
Tunables at the top of `ScratchEngine.h`: `kSlipTau` (catch stiffness — smaller = snappier)
and `kTouchReleaseHold` (cap-flicker ride). Re-tune against a fresh capture with
`make trace-replay TRACE=trace.csv RELEASE_HOLD=<secs>` then `trace-analyze`.

### Diagnosing on hardware — the trace capture

Touch *feel* only confirms on a real SC1000, so the plugin can log the raw inputs
and its own decisions for offline pattern-matching. Set `SC1000_TRACE=<path>` (read
in `PluginProcessor`) and every audio block appends one row — the firmware stream as
decoded (`rawTouch`, `jog`, `playing`) next to the gate's decision (`mode`, `motor`,
`pitch`, `playhead`) — via the realtime-safe `TraceLog` (pre-reserved buffer, flushed
to CSV on teardown). One file, one clock, so firmware stream and plugin behaviour are
already aligned.

```sh
make trace          # runs the Standalone with logging → trace.csv; scratch, then QUIT to flush
make trace-analyze  # classifies every mis-decision window
```

`tools/trace_analyze.py` is the payoff: for each "motor kept running while I moved"
window it reports whether the **cap bit was firing** — splitting a **gate bug**
(fixable in `TouchGate.h`) from a **hardware dropout** (PIC cap-sense, not fixable
host-side) — plus stationary-touch halts (symptom 2) and at-rest cap noise (symptom
3) and a histogram of forward-push dropout durations. `host/midimon.swift` (now
timestamped) is an independent raw-MIDI cross-check.

## Recommended stack

**JUCE** (AU + Standalone, macOS first): MIDI input, real-time audio, file
loading, and GUI in one framework. A Standalone build is the fastest way to test
against the hardware without a DAW.

### Plugin format — build **AU, never VST3** (decided)

This is load-bearing, not a preference. **VST3 does not pass raw MIDI CC to
plugins** — the host maps CC → declared *parameters* via `IMidiMapping`, which
normalizes the value and destroys event timing. That is **fatal for the relative
jog**: CC20's two's-complement deltas would arrive as mangled parameter jumps,
not a signed delta stream. Confirmed against the Steinberg VST3 SDK, the JUCE
forum, and a KVR DSP thread.

- **Target AU** (Audio Unit). JUCE delivers raw CC straight into the
  `MidiBuffer`. Native arm64, no Steinberg license needed. This is the primary
  build target.
- **VST2** also delivers raw CC, but needs the Steinberg VST2 license — only if
  you already hold it.
- **Standalone** for hardware testing (also gets raw CC).
- **Do not ship VST3** for this plugin; the core feature can't work through it.

### Routing the controller into the plugin (Renoise)

Assign the SC1000 as the **instrument's MIDI Input** in Renoise — the raw stream
is directed to the plugin component untouched. Do **not** use Renoise's
MIDI-Mapping-to-parameter path; it normalizes CC the same way VST3 does and would
break the relative jog.

## Self-contained saving (decided)

Renoise reliably saves a hosted plugin's **own state chunk**
(`getStateInformation`) inside the `.xrns` song — verified behaviour, including
the full save/close/reopen round-trip. So: **embed the loaded sample's audio data
in the plugin state.** The sample then travels with the song, reproducing
Renoise's self-contained `.xrns` feel even though the audio lives inside the
plugin rather than the native sampler.

The embedded audio is stored as **FLAC** (since state version 2 — lossless to 24-bit,
~5× smaller than raw float PCM; v1 raw-PCM states still load). It is encoded only
on save and decoded once on load into the in-RAM PCM buffer the engine reads, so
realtime scratching is unaffected. The current **v3** chunk adds the shift-layer
params (pitch / curve / brake) ahead of the audio. The drag limitation (below) only concerns the
*initial* load gesture — once loaded by any means, the sample is bundled in the
song, so dragging *from* Renoise is never required (see the Renoise drag note).

> Caveat on the sample editor: Renoise's native sample editor only edits *native*
> samples, not one living inside a plugin. Workflow: prep/chop in Renoise's editor
> → drag into the plugin, which then owns + bundles it. The plugin needs its own
> minimal waveform view anyway (it's a headline feature the hardware lacks).

## USB-MIDI device name — "MIDI Gadget" (rename to "SC1000" deferred)

macOS shows the controller as **"MIDI Gadget"**, not "SC1000": the firmware uses
the legacy `g_midi` USB gadget, whose USB **product string is compiled into the
kernel** (`drivers/usb/gadget/legacy/gmidi.c`, `"MIDI Gadget"`) — it is not a
runtime setting. Renaming it is therefore a *firmware* change, not a plugin one.
The plugin binds by matching "gadget"/"f_midi" in the ALSA hints
(`firmware/overlay/software/sc_midi_out.c`, `open_gadget()`), so the name is
cosmetic to the plugin either way.

**How to do it (deferred — needs a rebuild + reflash + hardware retest):** patch
the product (and manufacturer) string in the kernel's `gmidi.c` to "SC1000", wired
through buildroot the same way `build/build.sh` already patches the device tree and
`.config` (a kernel patch in a buildroot global patch dir, or a post-extract
`sed`). A cleaner-but-bigger alternative is the configfs/libcomposite route the
kernel fragment already anticipates (`build/kernel-gadget.fragment`, the commented
`CONFIG_USB_CONFIGFS*` lines), which sets the gadget strings from userspace at
boot. Deferred because it can only be confirmed on hardware — best done alongside
the first real hardware test of the whole controller→plugin chain.

## Before building: sanity-check the off-the-shelf option

One existing plugin gets close: **Scratch Track** (Stagecraft, ~$100, AU + native
arm64, hosts in Renoise). It genuinely position-scrubs a user sample — reversible,
spin-backs, visible needle. **But its documented MIDI model is absolute-only**
(touch button + one absolute "Scratch" slider), with no relative-encoder mode, so
the SC1000's relative CC20 likely reads as position *jumps*, not scrubbing. It has
a 30-day free trial — worth ~1 hour (map CC20 to its Scratch slider, watch
forward/reverse) before committing to the custom build. A small relative→absolute
MIDI bridge, or reconfiguring the SC1000 firmware to emit an absolute jog CC,
could rescue this path. Everything else surveyed (DropZone, Turntablist Pro,
TAL-Sampler, granular plugins, Gross Beat-style tools) fails the "drop sample →
jog-scratch" test.

There is **no native-Renoise scrub** path — confirmed dead. Sample-start
modulation is sampled only at note-trigger; `0Sxx` is per-trigger and 256-step
coarse; the Formula device runs at tick/pattern rate; no meta device exposes
sample *position* as a real-time target. The custom plugin is the only route that
guarantees true scratch feel.

---

## Open questions (decide these first)

### Playback model
1. **Idle/at-rest behaviour** — when the jog isn't moving, should the sample be
   *silent/paused* (pure turntable scrub — only sounds when the platter moves),
   or *playing at 1×* (CDJ-style, jog only nudges)? Or a **mode toggle**?
   *(Lean: pure scrub for MVP — it matches the SC1000 and is what "scratch" means.)*
2. **Touch semantics** — ~~scratch only while `touched`? jog always scrub? release =
   stop/coast/hold?~~ **DECIDED (see "Touch sensing"):** `cap on` (or a stopped motor)
   ⇒ the jog is in control (moving = scratch, still = the record eases to a stop under
   the finger); a real **lift** (cap off) snaps *stiffly* to the motor, no momentum ride.
   *Still open:* a `kHeldMax` stuck-cap timeout — only if a unit's cap is found to linger
   on after a lift (this MK2 doesn't); it would trade off against deliberate long holds.
3. **Inertia/slip** — model platter momentum on release, or keep it simple (stop)?

### Controls
4. **Crossfader (CC16)** — ~~gate vs linear vs user-assignable? what curve?~~
   **DECIDED:** **double-cut (centre-open)** — full at the centre, silent at both
   edges (gates the single scratch voice; cut toward either end, SC1000-firmware
   default); the **curve** (sharp↔soft) is adjustable via the shift layer and shapes
   the centre→edge falloff; volume pot CC18 sets the level. (CC17 mirrors CC16 —
   ignore it.)
5. **Volume pots (CC18/19)** — **DECIDED:** CC18 = master level (also the target of
   the shift-layer "volume" mode, via `volA`). CC19 (volB) still spare — one voice.
6. **Buttons** — ~~assign to what?~~ **DECIDED (front controls):** Start/Stop (27)
   = transport; cue pads (32–35) = crossfader shift-layer mode select; Shift (25)
   = the layer gate (see "Crossfader shift layer"). *Still open:* the 4 back buttons
   (Notes 21–24) — candidates: load next/prev sample, cue points, reverse, loop.

### Sample & musical features
7. **Sample slots** — single sample, or multiple banks/slots switchable by buttons?
8. **Loading** — drag-and-drop only, file browser, both? Formats (wav/aiff/mp3/flac)?
9. **Cue points / loops** — in scope for MVP, or later?
10. ~~**Loop the sample** at its ends, or one-shot / clamp at boundaries?~~
    **DECIDED: loop** — the playhead wraps in both directions with cubic
    interpolation across the boundary (`ScratchEngine::wrap`/`readCubicLoop`).
    Toggleable via `setLoop`, default on. One-shot/clamp remains the `loop=false` path.

### Engine & I/O
11. **Latency budget** — scratch wants low latency (≈ <10 ms jog→audio). Buffer
    size limits? (Device MIDI is throttled to ~1 kHz; jog deltas accumulate.)
12. **Jog smoothing** — ignore isolated ±1 deltas / idle trickle, or pass raw?
13. **Audio** — **mono internally** (decided): samples are downmixed to one
    channel on import (`toMono` in `PluginProcessor.cpp`), stored/displayed mono,
    and fanned out to the stereo bus on playback. One waveform, half the data.
    Sample-rate handling: the engine resamples to host rate at playback time.
14. **MIDI device selection** — auto-bind to "MIDI Gadget", or user-pick? Handle
    hot-plug/reconnect (the SC1000 powers from USB, so it can come and go).

### Packaging
15. ~~**Formats** — VST3 + AU?~~ **DECIDED: AU + Standalone, never VST3** (VST3
    strips raw CC — see "Plugin format" above). VST2 only if the Steinberg license
    is already held.
16. **GUI scope for MVP** — waveform + playhead is the headline feature
    (the hardware has none). How much beyond that for v1 (cue markers, level meters)?
17. **Generic mode** — keep it usable with any controller sending this MIDI map,
    or SC1000-specific assumptions OK?

### Project
18. **License** — GPLv2-compatible (controller side is GPLv2)? Confirm before release.
19. ~~**Name** — keep `scratch-vst`?~~ **DECIDED: the plugin is "SC1000"**
    (`PRODUCT_NAME` / `getName` / the on-screen header), folded into the public
    `gherkins/sc1000-controller` repo under `vst/`. The internal AU codes stay
    `aumu Scr1 Scvt` and the FLAC state magic stays `'SCR1'` — invisible, no reason
    to churn. The repo keeps its established public name.
