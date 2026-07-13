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

### Jog → pitch: velocity mode (default) vs the position servo vs counts-per-block (classic)

Measuring hand velocity as **counts per block** quantizes the pitch: at 512-frame
blocks one jog count ≈ 4% of 1× speed, so slow drags and bursty MIDI render as an
audible FM staircase (a granular/warpy scratch). The engine's default since
2026-07-13 is **velocity mode** (`kVelTau`, mirrored from catski where it won the
hands-on A/B "by far"): the classic-deadbanded counts feed a **time-based EMA**
whose output is the pitch target directly. That alone kills the staircase; there
is deliberately **no position ledger** — the record trails the hand by the
smoothing lag and coasts it in after the stroke, never accelerating past the hand
to repay slip, and classic's ±1-per-block deadband knee (the slow-speed slipmat
compliance, dead below ~0.08×) is kept. Both were the difference between the
servo "feeling amplified" under the hands and feeling like vinyl.

The **position servo** (2026-07-12 → 2026-07-13 the default, now
`SC1000_SCRATCH_MODE=servo`) goes further: jog counts integrate into a hand
*position* which the pitch chases — smoothed velocity feedforward (`kServoVelTau`)
plus position-error repayment over `kServoCatch` (with `kScratchTau` the grab loop
is ζ≈0.7 — settles without ringing; `kServoErrMax` is the anti-windup clamp). ±1
count of position error is ~0.4 ms of record — inaudible — so the staircase
vanishes while displacement tracking stays exact, and the playhead still only ever
moves via speed/direction (no jumps). On the synthetic bursty worst case (a
constant-speed hand whose counts land every other block) the pitch ripple is ~4.5×
smaller at 512-frame blocks. Exactness is also what cost it the A/B: repaying the
banked lag means transiently running **above** the hand's speed — physically
un-vinyl, felt as amplification.

Two boundaries matter (for the servo; velocity mode needs neither — its EMA of
real counts *is* the freewheel's smoothed speed in the bridge, and it feeds on the
plain classic deadband):

- **The servo models the hand, so it only runs while the cap reports one.** In the
  touch gate's cap-off bridge the platter is a freewheel; chasing its position
  unwinds the servo's standing error as a pitch wobble right in the slipmat
  handover. Bridge blocks ride the counts-per-block path, so a release starts the
  motor catch from the platter's true speed.
- **Deadband**: the servo variant eats only a *time-isolated* ±1 count — one with no
  counts for `kServoTickGap` (10.6 ms) before it (at crawl speeds single counts are
  most of the signal; the idle trickle never follows recent motion) — while the
  classic path keeps the plain `kJogDeadband`. The window is time, not "previous
  block": per-block rules change meaning with the host block size (at 256-frame
  blocks a 94 c/s crawl is 1,0,1,0 — a prev-block test starved it dead; found when
  catski's 256-frame blocks exposed it, see below).

`SC1000_SCRATCH_MODE` (env var, read in `PluginProcessor`) selects the model:
`servo` for the position servo, `classic` for counts-per-block everywhere — the
verbatim original path. Keep classic intact: the catski Rust port
golden-trace-diffs its classic mode against ours 1:1, and `trace_replay` A/Bs
feel changes by replaying a capture through the models
(`make trace-replay MODE=velocity|servo|classic`).

**The slipmat write-off (the "claw-back" fix)**: on a **grab** (cap lands while the
motor spins at 1×), the record slips past the hand while the pitch settles; the raw
servo counted that slip as position error and repaid it — the record briefly played
*backward* (≈ −0.36 peak on a synthetic grab, ≈ −0.09 in the s8 capture) while the
hand dragged forward. A real record slips under the finger and never claws back. So:
the servo may brake the record to a stop but never reverse it against the hand — if
the target opposes the record's direction and the hand's own counts don't command
the reversal, the target parks at 0 and the un-repayable error is written off as
slip. The authority to reverse is **counts/s, block-size independent** (a per-block
count rule sat here first and parked genuine −1-per-block pulls at 256-frame blocks
— catski, which runs 256, reported it as "slips while scratching"): one block of
strong counts (≥ `kServoAuthNow` 375 c/s — a real stroke turnaround, instant) or the
smoothed **pure-hand velocity** (`servoHandV`, never seeded from the pitch, beyond
`kServoAuth` 100 c/s over `kServoAuthTau` 30 ms — genuine gentle pulls, within a
couple of blocks). Ripple ticks reach neither: a lone ±1 is at most 187 c/s
instantaneous and barely dents the EMA (in the s10 capture two −1s amid a forward
drag had dumped the banked slip as a −0.27 reversal) — the position integration
still keeps the ±1s. Genuine backspins (counts against the
record) pass through untouched (hard backspin reaches −0.5× in 21 ms, same as
without the clamp); bursty-ripple and displacement tracking are unchanged. Validated
on the s10-grabs capture (50 grabs at ~1×): raw servo claw-backs on 7 of 13
forward/hold grabs; with the write-off 0, worst post-grab pitch +0.001. The
s11-crawl capture puts the staircase win on real hardware data: ultra-slow crawls
(1–4 counts/block) halve the HF pitch ripple vs classic (σ 0.0152 → 0.0079) with
better displacement tracking (1.9 ms vs 5.3 ms worst); the clamp costs crawls
nothing (raw-servo vs write-off near-identical there). Tested but NOT adopted (add if a grab still
feels sticky on hardware): seeding `servoV = 0` instead of `pitch` when engaging
from a genuine release softens the near-stop on grab-and-slow-drag (min pitch
0.02 → 0.06 of a 0.165 hand speed) at the cost of extra state; the seed must stay
`pitch` for cap-flicker re-grabs mid-scratch, where pitch ≈ hand speed.

## Touch sensing — the gate (`TouchGate.h`)

The capacitive pad is a single noisy bit straight from the PIC (`sc_input.c`:
`capIsTouched = (result >> 4 & 0x01)`, shipped as CC21 + Note20). There is **no
sensitivity / threshold / hysteresis in the Linux firmware** to tune — that logic
lives in the PIC input processor (PIC18LF14K22, upstream `firmware/main.c`;
reflashable only with a PICkit via the main PCB's **ICSP header J8**, not via
the USB-stick updater). **Which PIC build is running matters** (learned
2026-07-07): the checked-in `firmware.hex` — what PICs are burned with — is
from **2018-12** and was never rebuilt, so the *source's* current algorithm
(2020-08: EMA-smoothed level α = 0.01, threshold = boot-baseline − 100,
20-sample flip debounce both ways) is **not** what this MK2 runs. The 2018
build in the unit: **no smoothing** (raw 10-bit samples straight against the
threshold), threshold calibrated once at boot (baseline minimum − 96 counts,
no hysteresis gap), touch latched after **3** consecutive low samples, release
after **200** consecutive high samples. So a mid-scratch dropout means the raw
signal genuinely sat above threshold for 200 straight samples — real coupling
loss and/or a boot-calibrated threshold gone thin with drift
(grounding/humidity/hand), fitting the observed session-dependent dropouts.
**The CC 22 firmware build forwards the analog level behind the bit (PIC I2C
regs 6/7) as CC 22** (`>> 3`, lower = more touch; see `MIDI-MAPPING.md`) — but
those registers only exist in post-2019-05 PIC firmware: **on this MK2, CC 22
reads constant 0 until the PIC is reflashed** (verified live: 72 s capture, 28
touch transitions, CC 22 flat). The bit alone is **not** usable
as the sole scratch gate, and a 107 s capture (`make trace`, analysed by
`trace_replay`) said exactly how badly:

```
cap-on rate:   backward pull 91%    forward push 58%    (the bit drops on the push)
66 forward-push dropouts mid-scratch, median 203 ms, up to ~1.16 s,
   with the jog CLEARLY still moving the whole time
only 4 genuine let-gos — and on a let-go the light platter settles
   (jog → 0) within ~21 ms        [WRONG — see 2026-07-07 correction below]
```

So gating on the cap bit handed control back to the motor in the middle of 58
scratches in that capture ("it keeps running while I scratch"). The split is
**directional**: the cap is unreliable on a forward **push** (it drops, ~58 % on) but
reliable on a backward **pull** (~91 % on).

> **Correction (2026-07-07):** the "~21 ms settle" above was measured on gentle lifts
> from slow/held platters and does **not** generalise: a fresh capture shows a platter
> released at speed **freewheels for > 1.2 s** (constant-deceleration coast,
> a ≈ 2250 counts/s²). That killed both "release = platter stops" reasoning *and* the
> "motion = touch" idea (a freewheeling platter keeps `|jog| > deadband` for over a
> second). It also killed trusting the cap bit directly: the same class of captures
> logged **15 cap drops in 11 s** of normal scratching, clusters of 5 mid-pull, each a
> pitch yank from −1.7 to +1 (the "stutter"). The model below keys on the *only* motion
> a freewheel can't fake — acceleration or reversal — and defaults to release otherwise.

**The model — "release by default, acceleration negates the touch-off" (2026-07-07,
final).** `TouchGate` (JUCE-free, unit-tested in `test/touchgate_test.cpp`
[`make touchtest`]) follows the jog when:

- **motor stopped** — a dead record you cue by hand, or
- **cap on** — your finger is on the platter (moving = scratch, still = eases to a stop
  under the finger), or
- **cap off but the platter just accelerated or reversed** — motion a freewheel *cannot*
  produce, so a hand must be on it.

Otherwise cap-off **releases** — fast, at any speed, in any direction: after a short
flicker-hold (`kReleaseHold` ≈ 40 ms) the motor takes it via the firm slipmat catch
(`kSlipTau` ≈ 35 ms), ~90 ms to track speed. **Movement alone is never touch; only
unfakeable motion is.**

Why this shape, and not the many that preceded it (see the git history of `TouchGate.h`
for the full archaeology — a freewheel-physics predictor with tolerance + decay gate +
motor-match zone + deliberate-scratch band + bridge timeout): a *released* platter's
behaviour overlaps a *hand's* almost everywhere. It freewheels for > 1 s (so "released =
stopped" is false and "motion = touch" holds long after a let-go); its low-speed crawl
decays far slower than Coulomb friction (so a decay predictor mistakes a gentle let-go
for a drag and sags forever); the encoder's ±30 % per-rev ripple scales with speed (so
any fixed tolerance either stalls fast releases or misses slow drags). Every model that
tried to *classify the whole trajectory* traded one artifact for another across six
hardware sessions (S1 stutter, S2 wind-down stops, S3 slow-drag yanks, S4 flaky 1×
releases, S5 gentle-let-go sags, S6 "step backwards"). The user's insight cut through it:
a freewheel can **never speed up and never reverse** — that is the *only* signal that
cleanly separates hand from no-hand, so it is the only thing the gate keys on. Everything
else defaults to release.

Mechanics — hand evidence is any of:
- **re-acceleration** — `|v|` rising above its running minimum by `kAccelAbs` +
  `kAccelRel`·min (a freewheel never speeds up);
- **reversal** at speed (a freewheel never turns around);
- **anti-motor "not freewheeling"** — the motor only ever drives *forward*, so a platter
  moving *backward* (above `kSustainSpeed`) can't happen without a hand — **except** the
  brief backward coast just after a backspin release, which freewheels toward 0. So while
  moving backward, evidence fires *unless* the deceleration matches the free-coast rate
  (`kFreewheelDecel` ± `kDecelTol`). This is what holds the *steady* and *decelerating*
  stretches of a pull — jog −22 held flat, jog −85→−48 braking at 60× friction — that
  carry no re-acceleration to detect (the S8 pull-back stutter). It's deliberately
  **backward-only**: a forward coast (a release, or a same-direction push dropout) is
  never held by it, so forward let-gos stay snappy.

Any of these sets a `kHandHold` (≈ 200 ms) window that keeps the bridge alive; real
scratch strokes renew it every ~50–100 ms. A strong event while *already* released
**re-captures** control (`kCaptureFloor`) — this is how a forward-push dropout, cap fully
dead for the whole stroke, still tracks the hand. The subtlety that made or broke the
forward case: the velocity EMA (`kTouchVelTau` 60 ms) *lags*, so a release mid-push makes
it converge upward = a phantom "re-acceleration" that re-holds every release (the S6
"worse" feel — 250–500 ms sagging rides). Fixed by seeding: a leading estimate
(`kVelTauFast` 12 ms) snaps into the working estimate the instant the cap drops, so
there's no transient to fake.

Result across all eight sessions: **forward releases are a clean ~40–140 ms handoff to
track speed** (S7: down from ~180–235 ms sluggish spin-up — the "too weak motor / paused"
feel — via flicker-hold 100 → 40 ms and slip 80 → 35 ms; the user confirmed this
"natural"), and **pull-back stutters dropped from a long list to 0–2 slow/brief blips per
session** (S8: the anti-motor test holds sustained/braked pulls that used to hand +1×
against the pull for 200–280 ms). The stutter stays bridged, backspins release cleanly,
cap-dead strokes track via re-capture.

**Forward-flick let-slip — tried and reverted (S9/S10).** A flung forward flick (spin the
platter past the motor and let it slip) snaps to 1× the instant the cap drops rather than
riding its forward momentum down. A `kFlickRide` gate (ride the freewheel while forward
above ~1.3×) fixed that single flick and left every normal release untouched — BUT it
compromised the **rapid on/off release** technique (S10): a forward flick and a rhythmic
on/off tap are the *same signal* (forward, above motor, cap off); the flick-ride held the
forward pushes at ~1.5×, so the sample drifted/slipped forward instead of settling to 1×
each tap. The only thing that separates them is off-*duration* (a flick stays released
~800 ms, a tap ~130 ms), which isn't knowable at the drop without delaying the ride into a
jump. Per the user's "don't compromise the other fixes for this," the flick-ride was
**reverted**: a single flick snapping to 1× is an accepted cosmetic limit; the on/off
release settling correctly is not negotiable.

**Residual (accepted):** the *very slow tail* of a backward pull (jog −7 ≈ −0.3×, where a
hand-held pull and a dying backward freewheel become genuinely indistinguishable) can
still briefly hand to the motor — ≈1–2 slow/brief blips per pull-heavy session, matching
"pull-back worst case: still jumpy/stuttery, rather rare". A dead-still held platter with
a phantom cap-drop also releases (no motion = no evidence).

The deliberate costs, both accepted: a *perfectly steady* drag with the cap off (no
acceleration for > `kReleaseHold`) hands to the motor as a soft ~`kSlipTau` swell until
the next stroke re-captures — but real scratching is never steady, the strokes are the
evidence; and a phantom cap-drop on a **dead-still held** platter is indistinguishable
from a release (no motion, no evidence) and releases until you move or re-grip (the PIC
re-latches touch in ~3 samples). **Tape-stop** is still available as a slow-down (cap on
+ decelerating jog → follows it down to a held stop).

**Measured live (2026-07-12, `s12-stophold` capture, servo-era build):** the dead-still
cost is the *stop-hold stutter* — grab the playing record to a halt and just hold it, and
the cap bit drops out on the resting finger for **200–1000 ms at a stretch**; each drop
correctly reads as a release, the motor swells the record to 1× (`kSlipTau` ≈ 35 ms, so
even a 100 ms drop swells to ~0.9) and the returning cap brakes it again — stop / play /
stop. Both stop-hold attempts in the capture stuttered. No hold time fixes this: riding a
1 s blind gap would make every *real* let-go mushy for that second, and gentle micro-
wiggles don't qualify as hand evidence by design (`kCaptureFloor`). Workarounds: keep the
record *decisively* moving while holding (real strokes renew evidence), or use transport
Stop. **The clean fix is blocked on hardware:** the analog capsense level (CC 22) would
let the gate hold whenever the level stays near the touch threshold (a resting finger)
vs falling to baseline (a true lift) — but on the MK2 as shipped CC 22 reads a constant
0 until the PIC is reflashed (PICkit via ICSP header J8; see MIDI-MAPPING.md). When that
happens: decode CC 22 into `ControlState`, add it as a trace column, and gate stillness-
releases on the level.

Validated on captures (**all seven 2026-07-07 sessions replayed together** — always check a
tuning change against ALL of them, they stress different regimes; the fixtures live in
`vst/test/traces/`). Tunables at the top of `ScratchEngine.h`: `kAccelAbs`/`kAccelRel`
(hand-evidence sensitivity vs encoder ripple), `kHandHold` (how long one swell holds —
vs how fast a true release commits after the last stroke), `kReleaseHold` (flicker ride),
`kVelTauFast`/`kTouchVelTau` (the seed vs the working estimate), `kCaptureFloor`
(re-capture threshold), and `kSlipTau` (the soft catch — bigger masks residual wrong
calls, smaller snaps harder). Re-tune against a fresh capture with
`make trace-replay TRACE=trace.csv HANDHOLD=<secs> RELEASEHOLD=<secs>` then `trace-analyze`.

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
   stop/coast/hold?~~ **DECIDED (see "Touch sensing", final 2026-07-07):** `cap on`
   (or a stopped motor) ⇒ the jog is in control; cap **off** ⇒ release (soft slipmat
   catch), *unless* the platter accelerates or reverses (motion a freewheel can't make)
   ⇒ a hand is on it ⇒ keep scratching. Movement alone is never touch. *Still open:* a
   dead-still held platter with a phantom cap-drop still releases (no motion = no
   evidence) — the accepted residual.
3. **Inertia/slip** — ~~model platter momentum on release, or keep it simple (stop)?~~
   **DECIDED: no synthetic inertia.** Release commits fast (`kReleaseHold` ≈ 40 ms), then
   the firm slipmat catch (`kSlipTau` ≈ 35 ms) slews to the motor — ~90 ms to track
   speed, a prompt grab rather than a sluggish modelled coast.
3b. **Host-side touch detection from CC 22** — the CC 22 firmware build streams the
   PIC's smoothed capsense level (the analog signal behind the CC21 bit; lower =
   more touch). **Blocked on hardware (2026-07-07):** this MK2's PIC predates the
   regs-6/7 export, so CC 22 reads constant 0 (see "Touch sensing"). Unblocking =
   reflashing the PIC via ICSP header J8 with a PICkit — which also upgrades the
   detector itself (2018 raw-sample build → current smoothed source), and once a
   PICkit is in hand the PIC firmware becomes *ours* to improve: real hysteresis
   gap, adaptive re-baselining, exporting the raw level. If/when CC 22 is live:
   capture a dropout trace, check the margin when the bit flaps, then detect in
   the plugin — adaptive baseline while released + dual thresholds with a real
   hysteresis gap — using the PIC bit only as a fallback. Until then, host-side
   touch work continues on the CC 21 bit + jog context only (TouchGate).

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
