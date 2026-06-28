#pragma once

// "Stick slipmat" — the playback decision per audio block. Pure C++, no JUCE
// (unit-tested in test/touchgate_test.cpp, replayed in test/trace_replay.cpp).
//
// A finger on the platter means YOU are in control: while the cap reads on, the jog
// drives the playhead — moving it scratches, holding it still eases the record to a
// stop (a stationary record is silent). It is NOT handed back to the motor while you're
// touching. The instant you LIFT (cap → 0) it slips (stiffly) to the motor; it does not
// ride the platter's leftover momentum, so releasing reliably means "play the sample",
// which is what cutting is almost always about.
//
// Why trust the cap bit rather than also requiring movement? An earlier version followed
// the jog only on cap-on AND moving — to dodge a feared "cap lingers after lift" +
// creeping platter. But on this hardware the cap is reliable exactly where it matters: a
// capture (trace.csv) shows it sits ON through the whole of a backward PULL and drops
// cleanly to 0 on a real lift. The extra "&& moving" instead mis-fired at every slow
// point of a scratch — a turnaround, a slow pull — where the jog momentarily reads 0 (the
// ±1 idle deadband). With the cap still on, the gate would hand to the motor and yank the
// playhead toward +1× AGAINST the stroke: the "fighting the motor" feel. Trusting the cap
// fixes that and keeps release snappy (a real lift IS cap-off). The remaining hardware
// limit — the cap dropping mid forward-PUSH — is unchanged (cap-off either way, and the
// motor it hands to is +1×, the same direction as the push). See docs/ARCHITECTURE.md
// "Touch sensing".
//
// follow the jog when:
//   • motor stopped — a dead record you cue by hand (so "not running reads as touch" is
//     correct by design), OR
//   • cap on — your finger is on the platter (moving ⇒ scratch, still ⇒ hold/stop).
// Otherwise (motor running AND cap off = a lift) → slip (stiffly, see kSlipTau) to the
// motor. A short Coast hold rides brief cap flicker before it commits to the slip.
struct TouchGate
{
    double releaseHoldSec = 0.020; // brief hold that rides scratch reversals / cap flicker before snapping to the motor

    enum class Mode
    {
        Released, // slip (stiff) to the motor — you're not actively scratching
        Scratch,  // follow the jog (finger on a moving platter, or cue-by-hand at a dead motor)
        Coast     // momentary hold (a reversal / flicker) before committing to the slip
    };

    void configure (double sampleRate) noexcept { rate = sampleRate > 0.0 ? sampleRate : 44100.0; }

    void reset() noexcept
    {
        releaseSamples = 0;
        engaged = false;
    }

    bool isEngaged() const noexcept { return engaged; }

    // One call per audio block.
    //   rawTouch     — the capacitive pad bit (CC21 / Note20).
    //   jog          — the (deadbanded) jog delta this block; 0 = the platter is still.
    //                  No longer part of the follow decision (finger-on alone decides);
    //                  retained for the trace/replay harness and a possible jog-bridge.
    //   motorStopped — the virtual motor is not driving the platter (speed ≈ 0).
    Mode process (int blockSamples, bool rawTouch, int jog, bool motorStopped) noexcept
    {
        (void) jog;
        const int releaseN = (int) (releaseHoldSec * rate);

        // Finger on the platter (cap on) — or a dead motor you cue by hand — follows the
        // jog: moving ⇒ scratch, still ⇒ the record eases to a stop UNDER your finger
        // (never yanked to the motor while touched). Only a real lift (motor running, cap
        // off) snaps back to the motor.
        const bool follow = motorStopped || rawTouch;

        if (follow)
        {
            releaseSamples = 0;
            engaged = true;
            return Mode::Scratch;
        }

        releaseSamples += blockSamples;
        if (releaseSamples < releaseN)
        {
            engaged = true;
            return Mode::Coast;
        }
        engaged = false;
        return Mode::Released;
    }

private:
    double rate = 44100.0;
    int  releaseSamples = 0;
    bool engaged = false;
};
