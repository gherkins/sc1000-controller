#pragma once

// "Stick slipmat" — the playback decision per audio block. Pure C++, no JUCE
// (unit-tested in test/touchgate_test.cpp, replayed in test/trace_replay.cpp).
//
// The platter follows the jog ONLY while you're actively scratching it (a finger on a
// MOVING platter). The instant you stop driving it — lift, or just stop moving — it
// snaps back to the motor. It deliberately does NOT ride the platter's leftover
// momentum: you give up the "push it forward and let it coast" trick, but releasing
// reliably means "play the sample", which is what cutting is almost always about.
//
// Why not honour the momentum (a real slipmat would)? On this hardware the capacitive
// pad lingers ON for up to ~1 s after you lift, AND the light platter keeps creeping —
// so "follow the momentum / wait for cap-off" reads as a sluggish drag, or a dead
// stillstand, before the motor catches. Snapping to the motor whenever you're not
// actively scratching is what actually feels right here.
//
// follow the jog when:
//   • motor stopped — a dead record you cue by hand (so "not running reads as touch" is
//     correct by design), OR
//   • cap on AND the platter is moving — you're scratching.
// Otherwise → slip (stiffly, see kSlipTau) to the motor. A short Coast hold rides
// scratch reversals and brief cap flicker before it commits to the slip.
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
    //   motorStopped — the virtual motor is not driving the platter (speed ≈ 0).
    Mode process (int blockSamples, bool rawTouch, int jog, bool motorStopped) noexcept
    {
        const int  releaseN = (int) (releaseHoldSec * rate);
        const bool moving   = (jog != 0);

        // Only an actively-scratched platter (finger on + moving) or a dead motor follows
        // the jog. A still platter — or one coasting after you lifted — snaps to the motor.
        const bool follow = motorStopped || (rawTouch && moving);

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
