# Architecture & open questions

Design notes for the plugin. An **MVP is now built** (`src/`, builds AU +
Standalone, passes `auval`) — the signal-flow sketch below matches the code. The
"Open questions" section still holds the decisions not yet settled; several are
marked with the provisional answer the MVP ships.

## Status — what the MVP implements

- **MIDI decode** (`PluginProcessor.cpp`): CC16 crossfader, CC18/19 volumes,
  CC20 relative jog, Note20 touch — from raw bytes, channel-agnostic. **Transport:
  the Start/Stop taps (Notes 26/27) and the 4 cue pads (Notes 32-35) toggle
  play/stop** on the press edge. Back buttons 21-24 and Shift 25 reserved.
- **Scratch engine** (`ScratchEngine.h`): reversible, variable-rate cubic-
  interpolated playback; jog→time via `kPlatterSpeed = 2275`. **Motor model ported
  from the SC1000 firmware** (`player.c`): **touch the platter → jog drives the
  playhead directly, motor paused** (you hold/scratch the record); **release →
  platter slips back to the motor** (slipmat inertia, `kSlipTau`); **playing →
  spins at 1×**; **stop → motor brakes 1×→0 over ~0.6 s = tape stop** (`kBrakeSpeed`).
  Transport toggled by the cue / Start buttons. A DC blocker keeps a stationary
  platter silent + de-clicks. Idle-trickle ±1 suppressed. Ends **clamp** (open q10).
  One voice. *(Note: scratch is now touch-gated — jog without touch is treated as
  coast, handled by the slip model.)*
- **Crossfader** (open q4, decided) = firmware-ported **hysteresis CUT** — sharp
  on/off at the closed edge + ~20 ms de-click, *not* a linear fade; the **volume
  pot CC18** sets the level. Side benefit: immune to the CC16 idle-drift. volB
  spare (open q5).
- **Self-contained saving**: the loaded sample is embedded in the plugin state
  chunk — confirmed approach (see below).
- **GUI** (`PluginEditor.*`): waveform+playhead, spinning platter w/ DJ marker,
  drag-and-drop + file-open loading.

**Not yet done / next:** more hardware tuning of the feel; Shift-layer actions +
back/cue button assignments beyond transport; loop mode; latency tuning. Tuning
knobs (all in `ScratchEngine.h`): `kPlatterSpeed` (scrub speed), `kBrakeSpeed`
(tape-stop length), `kSlipTau` (slipmat catch-up), `kFaderOpenPt`/`kFaderClosePt`
(cut point), `kFaderDecay` (cut de-click), and the jog direction (CC20 sign).

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

> Caveat on the sample editor: Renoise's native sample editor only edits *native*
> samples, not one living inside a plugin. Workflow: prep/chop in Renoise's editor
> → drag into the plugin, which then owns + bundles it. The plugin needs its own
> minimal waveform view anyway (it's a headline feature the hardware lacks).

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
4. **Crossfader (CC16)** — gate (hard cut, for transforms/chirps) vs linear gain
   vs user-assignable? What curve? Does it gate just the scratch voice or a mix?
   (CC17 mirrors CC16 — ignore it.)
5. **Volume pots (CC18/19)** — what do they control? Master out? Sample level vs a
   second source? Right now there's only one sample voice, so one may be spare.
6. **Buttons (Notes 21-24)** — assign to what? Candidates: load next/prev sample,
   set/jump cue, start/stop, reverse, loop in/out. (Front Start/Shift + cue
   buttons aren't emitted yet — see MIDI-MAPPING "Not yet emitted".)

### Sample & musical features
7. **Sample slots** — single sample, or multiple banks/slots switchable by buttons?
8. **Loading** — drag-and-drop only, file browser, both? Formats (wav/aiff/mp3/flac)?
9. **Cue points / loops** — in scope for MVP, or later?
10. **Loop the sample** at its ends, or one-shot / clamp at boundaries?

### Engine & I/O
11. **Latency budget** — scratch wants low latency (≈ <10 ms jog→audio). Buffer
    size limits? (Device MIDI is throttled to ~1 kHz; jog deltas accumulate.)
12. **Jog smoothing** — ignore isolated ±1 deltas / idle trickle, or pass raw?
13. **Audio** — stereo throughout (sample is stereo). Sample-rate handling
    (resample sample to host rate on load).
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
19. **Name** — keep `scratch-vst`?
