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
  `ScratchEngine::setLoop`, default on). One voice. *(Note: scratch is now
  touch-gated — jog without touch is treated as coast, handled by the slip model.)*
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

## Touch sensing — forward-push dropout (known hardware limitation)

Measured live (via the continuous CC21 touch level — see MIDI-MAPPING.md — and a
decoding monitor): **the capacitive pad reliably drops touch for ~200–500 ms during
the *forward push* of a scratch, while holding solid on the backward pull.** It is
not MIDI loss or flicker — the underlying sensor bit itself reads 0. The touch line
is a single bit straight from the PIC (`sc_input.c`: `capIsTouched = (result >> 4 &
0x01)`); there is **no sensitivity / threshold / hysteresis in the Linux firmware**
to tune — that logic lives in the PIC microcontroller (separate, not in either repo).

This creates an **irreducible tradeoff** for the host engine, because a 300 ms
forward-push dropout (touch off, jog moving) is indistinguishable from a post-release
**coast** (touch off, jog moving):

- **Bridge the dropout** (jog-as-touch-proxy, a long coast window, or a firmware
  `capIsTouched` debounce) → scratches never break, but the same bridge lets the
  light platter's coast keep driving playback after you let go — the record "flies"
  forward / drifts back instead of catching the motor.
- **Don't bridge it** (gate purely on touch) → release is clean, but the motor takes
  back over mid-scratch on the longest forward-push dropouts.

No coast-window value threads the needle. **Decision: gate on touch only** (clean
release is the priority; the jog is deliberately *not* used as a touch proxy — see
the engine comment and git history). The `kTouchDebounce` window (40 ms) holds the
platter *speed* through brief drops, which on a forward push roughly continues the
push; raising it trades a longer release tail for fewer takeovers (the lever if this
is revisited). The only real fix — making touch not drop during scratching — is at
the **PIC capacitive sensing** level (sensitivity / hysteresis), a separate firmware
and a bigger job. Accepted as a known limitation for now.

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
2. **Touch semantics** — scratch only while `touched` (Note20) is on? Or does the
   jog always scrub? On release: hard stop, **coast with inertia** (emulate the
   weighted platter — SC1000 has `slippiness`/`brakespeed`), or hold position?
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
