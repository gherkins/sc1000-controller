#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

#include "ControlState.h"
#include "TouchGate.h"
#include "TraceLog.h"

// Variable-rate, reversible sample playback driven by the jog — the "treat a
// sample like vinyl" core. The motor/slipmat/brake model and the crossfader cut
// are ported from the SC1000 firmware (rasteri/SC1000 player.c + sc_input.c) so
// the feel matches the hardware:
//
//  • Touch the platter  → the jog drives the playhead directly; the motor is
//    paused (you hold/scratch the record). Release → it slips back to the motor.
//  • Motor (playing)    → spins at 1×; the platter eases toward it (slipmat,
//    firmware `slippiness`).
//  • Stop               → the motor brakes from 1× to 0 over ~0.6 s = tape stop
//    (firmware `brakespeed`).
//  • Crossfader         → a double-cut (centre-open, not a linear fade): full at the
//    centre, silent at BOTH edges — the SC1000 "battle" fader where you cut toward
//    either end rather than mixing between two decks. ~20 ms de-click decay; the
//    curve (cue 3) shapes the falloff and the volume pot (CC18) sets the level.
//
// A one-pole DC blocker keeps a stationary platter silent and de-clicks.
class ScratchEngine
{
public:
    static constexpr double kPlatterSpeed = 2275.0; // jog counts per audio-second (≈ 33 rpm)
    static constexpr double kScratchTau   = 0.020;  // jog→pitch smoothing (s) — kills per-block MIDI jitter
    // Strict hand-evidence touch gate (TouchGate.h): cap-off RELEASES fast — always —
    // unless the platter accelerates or reverses (impossible for a freewheel), which
    // negates the touch-off and keeps the jog in control. Movement alone is never
    // touch; only motion a freewheel cannot produce is.
    static constexpr double kAccelAbs    = 300.0; // counts/s — the |v| rise (above its running minimum) that
                                                  // counts as hand evidence; must clear the smoothed ripple
    static constexpr double kAccelRel    = 0.20;  // + fraction of the running minimum (ripple scales with speed)
    static constexpr double kFreewheelDecel = 2250.0; // counts/s² — the platter's free-coast deceleration; at
                                                  // speed a freewheel slows at exactly this — anything else = hand
    static constexpr double kSustainSpeed = 500.0;  // counts/s (≈0.22×) — floor for the anti-motor (backward)
                                                  // "not freewheeling" test; below = idle, ignore encoder noise
    static constexpr double kDecelTol    = 0.6;   // fraction — how far a BACKWARD platter's decel may sit from
                                                  // the freewheel rate and still read as a backspin release
    static constexpr double kHandHold    = 0.200; // s — one piece of evidence keeps the bridge alive this long
                                                  // (real strokes re-accelerate every ~50–100 ms)
    static constexpr double kReleaseHold = 0.040; // s — evidence-free time before a release commits. Short:
                                                  // it only rides a 2–3 block cap glitch; real scratches are
                                                  // held by the accel/reversal test, not this. Longer just
                                                  // makes the release track the coasting platter (mushy)
    static constexpr double kVelTauFast  = 0.012; // s — leading velocity estimate (seeds vHat at the drop)
    static constexpr double kCaptureFloor= 600.0; // counts/s — re-capture from RELEASED needs real motion
    static constexpr double kTouchVelTau = 0.060; // s — platter-velocity smoothing for the evidence test
    static constexpr double kBrakeSpeed   = 3000.0; // firmware brakespeed — bigger = longer tape stop
    static constexpr double kSlipTau      = 0.035;  // slipmat catch time (s) — release slews to the motor. Firm
                                                    // (~35 ms) so the motor takes over promptly instead of a
                                                    // sluggish "too weak motor" spin-up. Not instant, to de-click
                                                    // the handoff; wrong releases are rare now (accel-gated)
    static constexpr double kFaderOpenPt  = 0.010;  // double-cut: opens when centre-distance exceeds this
    static constexpr double kFaderClosePt = 0.004;  // ...closes below this (hysteresis, near each edge)
    static constexpr double kFaderDecay   = 0.020;  // crossfader cut decay (s) — de-click
    static constexpr double kVolumeGain   = 1.0;    // output level vs |pitch| (firmware VOLUME): ≥1× → full, ~0 stopped
    static constexpr int    kJogDeadband  = 1;      // ignore isolated ±1 jog counts (encoder idle trickle)
    static constexpr double kCurveSharpKnee = 0.01; // fader travel to reach full at the sharpest curve (≈ hard cut)
    static constexpr double kPitchTau       = 0.030;// pitch-fader (varispeed) smoothing (s) — de-click step changes

    // --- position-servo jog→pitch (the default scratch model) ---
    // Measuring hand velocity as counts-per-block makes one jog count ≈ 8% of 1×
    // speed at 256-frame blocks, so slow drags and bursty MIDI render as an audible
    // FM staircase (the "granular" scratch sound). The servo treats the encoder the
    // way the SC1000's own xwax firmware does: integrate the counts into a hand
    // *position* and chase it — smoothed velocity feedforward plus position-error
    // repayment. ±1 count of position error is ~0.4 ms of record, inaudible, so the
    // staircase vanishes while displacement tracking stays exact (the playhead
    // still only ever moves via speed/direction — no jumps). With kServoCatch=0.040
    // and kScratchTau=0.020 the grab-and-hold loop is ζ≈0.7 — settles without
    // ringing. The servo models the HAND, so it only runs while the cap reports
    // one: in the gate's cap-off bridge the platter is a freewheel, and chasing its
    // position unwinds the standing error as a pitch wobble right in the slipmat
    // handover — those blocks ride the counts-per-block path instead.
    // SC1000_SCRATCH_MODE=classic restores counts-per-block everywhere (the
    // verbatim pre-servo path — required when diffing golden traces against the
    // catski port, whose classic mode mirrors it 1:1).
    static constexpr double kServoVelTau = 0.060; // s — hand-velocity feedforward smoothing
    static constexpr double kServoCatch  = 0.040; // s — position error repaid over this horizon
    static constexpr double kServoErrMax = 0.150; // s of record — anti-windup clamp on the lag

    void prepare (double hostSampleRate)
    {
        hostRate = hostSampleRate;
        const double fc = 12.0; // DC-blocker high-pass cutoff (Hz)
        dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / hostRate);
        dcX.fill (0.0f);
        dcY.fill (0.0f);
        pitch = 0.0;
        pitchScaleSm = 1.0;
        motorSpeed = 0.0;
        faderVol = 0.0;
        faderOpen = false;
        servoErr = 0.0;
        servoV = 0.0;
        servoEngaged = false;
        prevJogActive = false;
        touchGate.accelAbs     = kAccelAbs;
        touchGate.accelRel     = kAccelRel;
        touchGate.freewheelDecel = kFreewheelDecel;
        touchGate.sustainSpeed = kSustainSpeed;
        touchGate.decelTol     = kDecelTol;
        touchGate.handHold     = kHandHold;
        touchGate.releaseHold  = kReleaseHold;
        touchGate.velTauFast   = kVelTauFast;
        touchGate.captureFloor = kCaptureFloor;
        touchGate.velTau       = kTouchVelTau;
        touchGate.configure (hostRate);
        touchGate.reset();
    }

    // Choose the jog→pitch model: true = position servo (the default — smooth slow
    // drags), false = the counts-per-block path (SC1000_SCRATCH_MODE=classic).
    // Call before audio starts (the processor constructor) — not thread-safe live.
    void setServo (bool on) noexcept { servo = on; }

    // Debug capture (off unless SC1000_TRACE is set — see TraceLog.h). enableTrace
    // pre-reserves off the audio thread; dumpTrace writes the CSV on teardown.
    void enableTrace (const std::string& path) { traceLog.enable (path, hostRate); }
    void dumpTrace()                           { traceLog.dump(); }

    void setSample (std::shared_ptr<juce::AudioBuffer<float>> buf, double srcRate)
    {
        const double len = (buf != nullptr && srcRate > 0.0)
                               ? (double) buf->getNumSamples() / srcRate
                               : 0.0;
        const juce::SpinLock::ScopedLockType sl (lock);
        sample = std::move (buf);
        srcRateA.store (srcRate > 0.0 ? srcRate : hostRate);
        lengthSec.store (len);
        playhead.store (0.0);
    }

    double getPlayheadSeconds() const noexcept
    {
        const double r = srcRateA.load();
        return r > 0.0 ? playhead.load() / r : 0.0;
    }
    double getLengthSeconds() const noexcept { return lengthSec.load(); }
    bool hasSample()          const noexcept { return lengthSec.load() > 0.0; }

    // Loop the sample at its ends (wrap) vs clamp/one-shot. Defaults to looping.
    void setLoop (bool shouldLoop) noexcept { loopOn.store (shouldLoop, std::memory_order_relaxed); }
    bool isLooping()        const noexcept  { return loopOn.load (std::memory_order_relaxed); }

    void process (juce::AudioBuffer<float>& out, ControlState& cs)
    {
        out.clear();
        const int n     = out.getNumSamples();
        const int numCh = out.getNumChannels();

        std::shared_ptr<juce::AudioBuffer<float>> buf;
        {
            const juce::SpinLock::ScopedTryLockType tl (lock);
            if (! tl.isLocked())
                return; // sample swap in progress — silent for one block
            buf = sample;
        }
        if (buf == nullptr || buf->getNumSamples() < 4)
            return;

        const double srcRate = srcRateA.load();
        const double base1x  = (double) n * srcRate / hostRate; // source samples for 1× this block

        const bool playing = cs.playing.load (std::memory_order_relaxed);
        const bool loop    = loopOn.load (std::memory_order_relaxed);

        // --- jog: ignore the isolated ±1 idle trickle the encoder emits at rest ---
        const int rawJog = cs.consumeJog();
        const int jog    = (std::abs (rawJog) <= kJogDeadband) ? 0 : rawJog;
        // Servo variant: eat only an *isolated* ±1 — at crawl speeds single counts
        // are most of the signal, and the idle trickle never follows a moving block.
        const int jogServo = (std::abs (rawJog) <= kJogDeadband && ! prevJogActive) ? 0 : rawJog;
        prevJogActive = rawJog != 0;

        // --- virtual motor: 1× when playing; brakes to 0 on stop (tape stop) ---
        // Computed BEFORE the touch gate because the gate needs to know whether the
        // motor is driving the platter (firmware: a stopped motor ⇒ the jog scrubs).
        if (playing)
        {
            motorSpeed = 1.0;
        }
        else
        {
            // brakeScale (cue 4) stretches/shortens the tape stop: bigger = longer.
            const double brakeLen = kBrakeSpeed * (double) cs.brakeScale.load (std::memory_order_relaxed);
            const double dec = (double) n * 48000.0 / (brakeLen * 10.0 * hostRate);
            motorSpeed = (motorSpeed > dec) ? motorSpeed - dec : 0.0;
        }
        const bool motorStopped = motorSpeed < 1.0e-6; // motor not driving the platter

        // --- touch gate (TouchGate.h): the capacitive bit (CC21/Note20) is a single
        // noisy bit that drops mid-scratch, so cap-off alone doesn't release: the gate
        // keeps following the jog until the platter provably moves like an UNTOUCHED one
        // (constant-deceleration freewheel) and only then slips to the motor. Follow the
        // jog whenever touched, when the motor is idle, or while bridging a cap drop.
        // See TouchGate.h and docs/ARCHITECTURE.md "Touch sensing".
        const bool rawTouch = cs.touched.load (std::memory_order_relaxed);
        const TouchGate::Mode touchMode = touchGate.process (n, rawTouch, jog, motorSpeed);

        // --- platter speed: scratch → follow the jog (finger on; still jog ⇒ target 0, so
        //     a held platter eases to a stop rather than fighting the motor);
        //     coast → hold speed briefly (riding a cap-off flicker before a real release);
        //     released → slip toward the motor (a real lift). ---
        const double dt = (double) n / hostRate;
        // The servo models the HAND — it only runs while the cap reports one. In
        // the gate's cap-off bridge (and the hand-evidence fallback) the platter
        // is freewheeling: chasing its position unwinds the servo's standing error
        // as a pitch wobble right in the slipmat handover, so those blocks ride
        // the counts-per-block path instead.
        const bool servoScratch = servo && rawTouch && touchMode == TouchGate::Mode::Scratch;
        if (servoScratch && ! servoEngaged)
        {
            servoErr = 0.0; // the hand position is defined from the grab
            servoV   = pitch;
        }
        servoEngaged = servoScratch;

        double targetPitch, tau;
        if (servoScratch)                                 // scratching: chase the hand position
        {
            const double hand = (double) jogServo / kPlatterSpeed; // record-seconds moved
            servoErr = juce::jlimit (-kServoErrMax, kServoErrMax, servoErr + hand);
            servoV  += (1.0 - std::exp (-dt / kServoVelTau)) * (hand / dt - servoV);
            targetPitch = servoV + servoErr / kServoCatch;
            // Slipmat write-off: the servo may brake the record to a stop but never
            // spin it BACKWARD against the hand. A record grabbed at speed slips
            // under the finger while the pitch settles; that slip is real slipmat
            // slip, not position debt — repaying it reversed the record on every
            // grab (the "claw-back": pitch 1.0 → −0.09 in the s8 capture while the
            // hand dragged forward). If the target opposes the record's direction
            // and the hand's own counts don't command the reversal, park the target
            // at 0 and write the un-repayable error off as slip. Genuine backspins
            // (hand counts against the record) pass through untouched — but the
            // authority to reverse needs a REAL count (> kJogDeadband): a lone ±1
            // tick is encoder ripple (s10 capture: two −1s amid a forward drag
            // dumped the banked slip as a −0.27 reversal), and classic's deadband
            // never acted on ±1 either. The position integration keeps the ±1s.
            const int recDir  = (pitch > 0.0) - (pitch < 0.0);
            const int handDir = (jogServo > kJogDeadband) - (jogServo < -kJogDeadband);
            if (recDir != 0 && handDir != -recDir && targetPitch * (double) recDir < 0.0)
            {
                targetPitch = 0.0;
                servoErr = (0.0 - servoV) * kServoCatch; // slip the slipmat already ate
            }
            tau = kScratchTau;
        }
        else if (touchMode == TouchGate::Mode::Scratch)   // scratching: follow the jog
        {
            targetPitch = (base1x > 1.0e-9) ? ((double) jog / kPlatterSpeed * srcRate) / base1x : 0.0;
            tau = kScratchTau;
        }
        else if (touchMode == TouchGate::Mode::Coast)     // settling after let-go: hold momentum
        {
            targetPitch = pitch;
            tau = kScratchTau;
        }
        else                                              // released: slip toward the motor
        {
            targetPitch = motorSpeed;
            tau = kSlipTau;
        }
        const double alpha    = 1.0 - std::exp (-(double) n / (tau * hostRate));
        const double pitchOld = pitch;
        pitch += alpha * (targetPitch - pitch);
        pitch  = juce::jlimit (-20.0, 20.0, pitch); // safety clamp (firmware ±20)
        if (servoScratch)
        {
            // Repay what actually played this block (trapezoid — matches the
            // playhead integration below).
            servoErr -= 0.5 * (pitchOld + pitch) * dt;
        }

        // --- crossfader DOUBLE-CUT (centre-open, hysteresis + decay); volume pot sets
        //     the level. Driven by distance from centre: c = 1 at the centre, 0 at
        //     BOTH edges, so a cut fires toward either end (the SC1000 battle fader) —
        //     not a side-to-side fade between decks. The curve (cue 3) shapes the
        //     falloff: sharp = full across nearly all travel, hard cut only at the very
        //     edges; soft = a gradual tent from the centre outward. ---
        const double fpos  = cs.crossfader.load (std::memory_order_relaxed);
        const double c     = 1.0 - 2.0 * std::abs (fpos - 0.5); // 1 = centre, 0 = either edge
        const double cutPt = faderOpen ? kFaderClosePt : kFaderOpenPt;
        faderOpen = (c >= cutPt);
        double faderTarget;
        if (! faderOpen)
        {
            faderTarget = 0.0; // true silence at either edge
        }
        else
        {
            const double curve = juce::jlimit (0.0, 1.0, (double) cs.faderCurve.load (std::memory_order_relaxed));
            const double xn    = juce::jlimit (0.0, 1.0, (c - kFaderOpenPt) / (1.0 - kFaderOpenPt));
            const double knee  = kCurveSharpKnee + curve * (1.0 - kCurveSharpKnee); // 0.01 (sharp) .. 1.0 (soft)
            faderTarget = juce::jmin (1.0, xn / knee);
        }
        const double fStep = (double) n / (kFaderDecay * hostRate);
        if (std::abs (faderTarget - faderVol) <= fStep) faderVol = faderTarget;
        else                                            faderVol += (faderTarget > faderVol) ? fStep : -fStep;

        // --- output level: crossfader cut × volume pot, scaled by |pitch| like the
        //     firmware (VOLUME) so a stationary/creeping platter is silent (no low-
        //     pitch drone) and the level tracks scratch speed. Computed per sample
        //     from the instantaneous rate so it never zippers. ---
        const double volBase = (double) faderVol * cs.volA.load (std::memory_order_relaxed);
        const double pf      = (srcRate > 0.0) ? hostRate / srcRate : 0.0; // rate → pitch (1× units)

        // --- render: the rate ramps linearly across the block (continuous with
        //     the previous block) so it never steps — this is what kills the zipper. ---
        const int    srcLen = buf->getNumSamples();
        const int    srcCh  = buf->getNumChannels();
        const double pos0   = playhead.load (std::memory_order_relaxed);

        // pitch fader (cue 1): a block-smoothed varispeed multiplier on the playback
        // rate (scales motor + scratch alike, like a turntable pitch slider).
        const double psTarget = (double) cs.pitchScale.load (std::memory_order_relaxed);
        const double psAlpha  = 1.0 - std::exp (-(double) n / (kPitchTau * hostRate));
        pitchScaleSm += psAlpha * (psTarget - pitchScaleSm);

        const double rOld = pitchOld * pitchScaleSm * srcRate / hostRate; // src samples / output sample, block start
        const double rNew = pitch    * pitchScaleSm * srcRate / hostRate; // ...block end
        const double dr   = (rNew - rOld) / (double) n;

        for (int i = 0; i < n; ++i)
        {
            const double fi  = (double) i;
            const double ri  = rOld + dr * fi;                        // instantaneous rate
            const double pos = pos0 + rOld * fi + 0.5 * dr * fi * fi; // ∫₀ⁱ (rOld + dr·t) dt
            const double rp  = loop ? wrap (pos, srcLen) : pos;       // wrap read pos when looping
            const float  vg  = (float) (juce::jmin (1.0, std::abs (ri * pf) * kVolumeGain) * volBase);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const int    sc = juce::jmin (ch, srcCh - 1);
                const float* d  = buf->getReadPointer (sc);
                const float  s  = loop ? readCubicLoop (d, srcLen, rp) : readCubic (d, srcLen, rp);

                const size_t k = (size_t) juce::jmin (ch, (int) dcX.size() - 1);
                const float  y = s - dcX[k] + dcR * dcY[k]; // one-pole DC blocker
                dcX[k] = s;
                dcY[k] = y;

                out.setSample (ch, i, y * vg);
            }
        }

        double newPos = pos0 + (double) n * (rOld + rNew) * 0.5; // total advance = avg rate × n
        newPos = loop ? wrap (newPos, srcLen)
                      : juce::jlimit (0.0, (double) (srcLen - 1), newPos);
        playhead.store (newPos, std::memory_order_relaxed);

        // --- debug capture: firmware stream + this block's decision, time-aligned ---
        if (traceLog.enabled())
        {
            const int modeInt = (touchMode != TouchGate::Mode::Scratch) ? 0
                              : touchGate.isBridging()                  ? 2 : 1; // 2 = bridging a cap drop
            traceLog.push ({ traceLog.stamp (n), n, rawJog, jog, rawTouch ? 1 : 0,
                             playing ? 1 : 0, motorSpeed, motorStopped ? 1 : 0, modeInt,
                             touchGate.isEngaged() ? 1 : 0, pitch, getPlayheadSeconds() });
        }
    }

private:
    // Wrap a (possibly negative or out-of-range) position into [0, len) for looping.
    static double wrap (double pos, int len)
    {
        if (len <= 0) return 0.0;
        const double L = (double) len;
        pos = std::fmod (pos, L);
        return (pos < 0.0) ? pos + L : pos;
    }

    // Catmull-Rom cubic with wraparound indexing — interpolates seamlessly across the
    // loop boundary (reads the start samples when near the end). pos must be in [0, len).
    static float readCubicLoop (const float* d, int len, double pos)
    {
        const int   i1   = (int) pos;
        const float frac = (float) (pos - i1);
        auto at = [d, len] (int i) { i %= len; return d[i < 0 ? i + len : i]; };
        const float xm1 = at (i1 - 1), x0 = at (i1), x1 = at (i1 + 1), x2 = at (i1 + 2);

        const float a = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
        const float b =         xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c = -0.5f * xm1            + 0.5f * x1;
        return ((a * frac + b) * frac + c) * frac + x0;
    }

    // Catmull-Rom cubic interpolation (4-point). Falls back to nearest at the edges.
    static float readCubic (const float* d, int len, double pos)
    {
        if (pos <= 1.0 || pos >= (double) (len - 2))
        {
            const int idx = (int) juce::jlimit (0.0, (double) (len - 1), pos);
            return d[idx];
        }
        const int   i1   = (int) pos;
        const float frac = (float) (pos - i1);
        const float xm1 = d[i1 - 1], x0 = d[i1], x1 = d[i1 + 1], x2 = d[i1 + 2];

        const float a = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
        const float b =         xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c = -0.5f * xm1            + 0.5f * x1;
        return ((a * frac + b) * frac + c) * frac + x0;
    }

    double hostRate = 44100.0;

    std::atomic<double> srcRateA { 44100.0 };
    std::atomic<double> lengthSec { 0.0 };
    std::atomic<double> playhead { 0.0 };
    std::atomic<bool>   loopOn { true }; // loop at the sample ends (vs clamp)

    std::shared_ptr<juce::AudioBuffer<float>> sample;
    juce::SpinLock lock;

    float dcR = 0.0f;
    std::array<float, 8> dcX {};
    std::array<float, 8> dcY {};

    // Motor / slipmat / crossfader-cut / touch state (audio thread only).
    TouchGate touchGate;     // decides scratch vs. release from the cap bit + motor + jog
    double pitch      = 0.0; // current platter speed (1× units)
    bool   servo        = true;  // jog→pitch model: position servo (false = counts-per-block)
    double servoErr     = 0.0;   // record-seconds the playhead lags the hand (hand − played)
    double servoV       = 0.0;   // smoothed hand velocity (1× units)
    bool   servoEngaged = false; // was in servo-scratch last block (edge → resync to the hand)
    bool   prevJogActive = false; // last block's raw jog ≠ 0 (isolated-trickle deadband)
    double pitchScaleSm = 1.0; // smoothed varispeed multiplier (pitch fader, cue 1)
    double motorSpeed = 0.0; // motor target (1 playing, brakes to 0 on stop)
    double faderVol   = 0.0; // smoothed crossfader cut gain
    bool   faderOpen  = false;

    TraceLog traceLog; // debug capture (off unless SC1000_TRACE set)
};
