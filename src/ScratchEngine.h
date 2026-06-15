#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>

#include "ControlState.h"

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
//  • Crossfader         → a hysteresis CUT (not a linear fade): full off only at
//    the closed edge, with a ~20 ms de-click decay (firmware fader). The volume
//    pot (CC18) sets the level.
//
// A one-pole DC blocker keeps a stationary platter silent and de-clicks.
class ScratchEngine
{
public:
    static constexpr double kPlatterSpeed = 2275.0; // jog counts per audio-second (≈ 33 rpm)
    static constexpr double kScratchTau   = 0.020;  // jog→pitch smoothing (s) — kills per-block MIDI jitter
    static constexpr double kTouchDebounce= 0.040;  // coast window after a touch drop (s) — rides flicker, then slips to motor
    static constexpr double kBrakeSpeed   = 3000.0; // firmware brakespeed — bigger = longer tape stop
    static constexpr double kSlipTau      = 0.050;  // slipmat time constant (s) — platter eases to motor
    static constexpr double kFaderOpenPt  = 0.010;  // crossfader cut: opens above this (0..1)
    static constexpr double kFaderClosePt = 0.004;  // ...closes below this (hysteresis)
    static constexpr double kFaderDecay   = 0.020;  // crossfader cut decay (s) — de-click
    static constexpr double kVolumeGain   = 1.0;    // output level vs |pitch| (firmware VOLUME): ≥1× → full, ~0 stopped
    static constexpr int    kJogDeadband  = 1;      // ignore isolated ±1 jog counts (encoder idle trickle)

    void prepare (double hostSampleRate)
    {
        hostRate = hostSampleRate;
        const double fc = 12.0; // DC-blocker high-pass cutoff (Hz)
        dcR = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / hostRate);
        dcX.fill (0.0f);
        dcY.fill (0.0f);
        pitch = 0.0;
        motorSpeed = 0.0;
        faderVol = 0.0;
        faderOpen = false;
        touchActive = false;
        touchHolding = false;
        holdSamples = 0;
    }

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

        // --- touch with a short coast-debounce ---
        // Jog movement is deliberately NOT used as a touch proxy: the light platter
        // coasts (and the encoder picks up vibration) after release, so using it would
        // let that momentum keep driving playback — the record "flies" forward/back
        // instead of catching the motor. Instead, when touch drops we hold the platter
        // speed for a brief window: a real re-touch resumes the scratch seamlessly; if
        // the window expires it was a genuine release and we slip to the motor. Touch
        // is the continuous CC21 level from the ≥CC firmware build, so it gates cleanly.
        // KNOWN LIMITATION: the pad drops touch ~200–500 ms on *forward* pushes (PIC
        // sensor, not fixable host-side), so the motor can briefly take back over mid-
        // scratch. Accepted tradeoff vs the post-release coast "fly" — raising
        // kTouchDebounce trades a longer release tail for fewer takeovers. See
        // docs/ARCHITECTURE.md "Touch sensing — forward-push dropout".
        const bool rawTouch = cs.touched.load (std::memory_order_relaxed);
        if (rawTouch)                            { touchActive = true; touchHolding = false; }
        else if (touchActive && ! touchHolding)  { touchHolding = true; holdSamples = (int) (kTouchDebounce * hostRate); }
        else if (touchHolding)                   { holdSamples -= n; if (holdSamples <= 0) { touchHolding = false; touchActive = false; } }

        // --- virtual motor: 1× when playing; brakes to 0 on stop (tape stop) ---
        if (playing)
        {
            motorSpeed = 1.0;
        }
        else
        {
            const double dec = (double) n * 48000.0 / (kBrakeSpeed * 10.0 * hostRate);
            motorSpeed = (motorSpeed > dec) ? motorSpeed - dec : 0.0;
        }

        // --- platter speed: scratch → follow jog; brief touch drop → hold speed;
        //     released → slip toward the motor (the coast is NOT followed). ---
        double targetPitch, tau;
        if (touchActive && ! touchHolding)        // scratching: follow the jog
        {
            targetPitch = (base1x > 1.0e-9) ? ((double) jog / kPlatterSpeed * srcRate) / base1x : 0.0;
            tau = kScratchTau;
        }
        else if (touchHolding)                    // brief drop-out: coast (hold speed)
        {
            targetPitch = pitch;
            tau = kScratchTau;
        }
        else                                      // released: slip toward the motor
        {
            targetPitch = motorSpeed;
            tau = kSlipTau;
        }
        const double alpha    = 1.0 - std::exp (-(double) n / (tau * hostRate));
        const double pitchOld = pitch;
        pitch += alpha * (targetPitch - pitch);
        pitch  = juce::jlimit (-20.0, 20.0, pitch); // safety clamp (firmware ±20)

        // --- crossfader CUT (hysteresis + decay); volume pot sets the level ---
        const double fpos  = cs.crossfader.load (std::memory_order_relaxed);
        const double cutPt = faderOpen ? kFaderClosePt : kFaderOpenPt;
        faderOpen = (fpos >= cutPt);
        const double faderTarget = faderOpen ? 1.0 : 0.0;
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

        const double rOld = pitchOld * srcRate / hostRate; // src samples / output sample, block start
        const double rNew = pitch    * srcRate / hostRate; // ...block end
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
    bool   touchActive  = false; // capacitive touch latched (scratching)
    bool   touchHolding = false; // in the brief coast window after a touch drop-out
    int    holdSamples  = 0;     // samples left in the coast window
    double pitch      = 0.0; // current platter speed (1× units)
    double motorSpeed = 0.0; // motor target (1 playing, brakes to 0 on stop)
    double faderVol   = 0.0; // smoothed crossfader cut gain
    bool   faderOpen  = false;
};
