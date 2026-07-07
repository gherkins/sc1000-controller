#pragma once

#include <cmath>

// "Strict hand evidence" touch gate — the playback decision per audio block. Pure C++,
// no JUCE (unit-tested in test/touchgate_test.cpp, replayed in test/trace_replay.cpp).
//
// A finger on the platter means YOU are in control: while the cap reads on, the jog
// drives the playhead — moving it scratches, holding it still eases the record to a
// stop (a stationary record is silent). It is NOT handed back to the motor while you're
// touching.
//
// The cap bit, however, drops mid-scratch (measured 2026-07-07: clusters of 5 drops in
// 300 ms of a pull), and every attempt to bridge those drops by MODELLING the freewheel
// (Coulomb prediction + tolerance + decay gate + motor-match + band + timeout — see git
// history of this file for the full archaeology) traded one artifact for another: the
// released platter's behaviour overlaps hand behaviour almost everywhere — crawls don't
// decay like the model, ripple mimics re-acceleration, drags mimic freewheels, normal
// releases happen at every speed.
//
// What does NOT overlap (user's insight, session 6): a freewheeling platter can NEVER
// speed up, and can NEVER reverse. That is the only unfakeable hand signal. So:
//
//   • cap-off ⇒ RELEASE, fast (kReleaseHold flicker-hold, then the motor takes it via
//     the soft slipmat catch, kSlipTau) — at any speed, in any direction;
//   • UNLESS the platter ACCELERATES (|v| rises meaningfully above its running minimum
//     since the drop) or REVERSES — impossible for a freewheel ⇒ a hand is on it ⇒ keep
//     scratching; each new event renews the hold for kHandHold;
//   • a STRONG acceleration while already released RE-CAPTURES control (covers strokes
//     where the cap is fully dead — the forward-push dropouts).
//
// Costs, accepted deliberately: a perfectly steady drag with the cap off (no
// acceleration for > kReleaseHold) hands to the motor as a soft ~kSlipTau swell until
// the next stroke or re-touch re-captures; a phantom cap-drop on a DEAD-STILL held
// platter is indistinguishable from a release (no motion, no evidence) and the motor
// takes over until you move or re-grip. Real scratching is never steady — the strokes
// themselves are the evidence.
//
// Follow the jog when:
//   • motor stopped — a dead record you cue by hand, OR
//   • cap on — your finger is on the platter, OR
//   • cap off but the platter provably accelerated/reversed just now (hand evidence).
// Otherwise → the motor takes it.
struct TouchGate
{
    // Tunables (the engine overrides these from the kXxx constants in ScratchEngine.h).
    double velTau       = 0.060; // s — jog-velocity smoothing (rides the AS5601 per-rev ripple, ±30 % raw)
    double accelAbs     = 300.0; // counts/s — |v| must rise at least this above its running minimum
                                 // (and a reversal must reach this speed) to count as hand evidence;
                                 // must clear the smoothed encoder-ripple amplitude
    double accelRel     = 0.20;  // + this fraction of the running minimum (ripple scales with speed)
    double freewheelDecel = 2250.0; // counts/s² — a released platter's free-coast deceleration. AT SPEED a
                                 // freewheel decelerates at EXACTLY this rate; any other acceleration
                                 // profile (maintaining, speeding up, or braking harder) needs a hand.
    double sustainSpeed = 500.0; // counts/s (≈0.22×) — floor for the anti-motor test; a backward crawl below
                                 // this is treated as idle (avoids reacting to encoder noise near zero).
                                 // Backward motion is strong hand evidence on its own, so the floor is low.
    double decelTol     = 0.6;   // fraction of freewheelDecel — how far the measured deceleration may sit
                                 // from the freewheel rate and still count as "freewheeling" (→ release).
                                 // Outside [1−tol, 1+tol]·decel ⇒ a hand. Wide enough to swallow ripple.
    double handHold     = 0.200; // s — how long one piece of hand evidence keeps the bridge alive;
                                 // scratch strokes re-accelerate every ~50–100 ms, so real scratching
                                 // renews continuously
    double releaseHold  = 0.100; // s — evidence-free time after a drop before the release commits
                                 // (rides cap flicker)
    double velTauFast   = 0.012; // s — a LEADING velocity estimate. The main EMA lags ~2·velTau, so a
                                 // release mid-push makes it converge upward = a phantom "re-acceleration"
                                 // that re-holds every release. At the drop we snap vHat to this leading
                                 // value so there's no transient to fake — clean releases, instant re-accel.
    double captureFloor = 600.0; // counts/s — a re-capture from RELEASED needs the platter actually
                                 // moving (light brushes/noise must not halt a playing sample)

    enum class Mode
    {
        Released, // the motor has it — slip toward motorSpeed (soft catch, kSlipTau)
        Scratch,  // follow the jog (finger on, cue-by-hand at a dead motor, or fresh hand evidence)
        Coast     // kept for API stability — no longer produced
    };

    void configure (double sampleRate) noexcept { rate = sampleRate > 0.0 ? sampleRate : 44100.0; }

    void reset() noexcept
    {
        vHat = 0.0;
        vFast = 0.0;
        accHat = 0.0;
        runMin = -1.0;
        holdLeft = 0.0;
        offT = 0.0;
        lastDir = 0;
        wasOff = false;
        bridging = false;
        engaged = false;
    }

    bool isEngaged()  const noexcept { return engaged; }
    bool isBridging() const noexcept { return bridging; } // cap off but following (fresh hand evidence)

    // One call per audio block.
    //   rawTouch   — the capacitive pad bit (CC21 / Note20).
    //   jog        — the (deadbanded) jog delta this block; 0 = the platter is still.
    //   motorSpeed — the virtual motor speed in 1× units (1 playing, braking → 0).
    Mode process (int blockSamples, bool rawTouch, int jog, double motorSpeed) noexcept
    {
        const double dt = (blockSamples > 0) ? (double) blockSamples / rate : 1.0e-4;
        const bool motorStopped = motorSpeed < 1.0e-6;

        // Two velocity estimates (counts/s): vHat rides the encoder's per-rev ripple
        // (the working signal); vFast leads it (used only to seed vHat at the drop).
        const double target = (double) jog / dt;
        const double vPrev = vHat;
        vHat  += (1.0 - std::exp (-dt / velTau))     * (target - vHat);
        vFast += (1.0 - std::exp (-dt / velTauFast)) * (target - vFast);
        // Smoothed acceleration (counts/s²) — for the hard-brake test below.
        accHat += (1.0 - std::exp (-dt / velTau)) * ((vHat - vPrev) / dt - accHat);

        // Finger on the platter (cap on) — or a dead motor you cue by hand — follows the
        // jog: moving ⇒ scratch, still ⇒ the record eases to a stop UNDER your finger.
        if (motorStopped || rawTouch)
        {
            engaged = true;
            bridging = false;
            wasOff = false;
            offT = 0.0;
            holdLeft = 0.0;
            runMin = -1.0;
            lastDir = 0;
            return Mode::Scratch;
        }

        // First block after the cap drops: snap the (lagging) working estimate to the
        // leading one, so its convergence can't masquerade as a re-acceleration. Seed
        // accHat to the freewheel rate so the snap itself isn't read as hand braking.
        if (! wasOff)
        {
            wasOff = true;
            vHat = vFast;
            accHat = (vHat < 0.0 ? freewheelDecel : -freewheelDecel);
        }

        offT += dt;
        const double speed = std::abs (vHat);

        // --- strict hand evidence: only motion a freewheel cannot produce ---
        bool event = false;
        const int dir = (vHat > 0.0) - (vHat < 0.0);
        if (speed >= accelAbs)
        {
            if (lastDir != 0 && dir != 0 && dir != lastDir)
                event = true; // reversal at speed — a freewheel never turns around
            if (dir != 0)
                lastDir = dir;
        }
        if (runMin < 0.0 || speed < runMin)
            runMin = speed;
        const double rise = accelAbs > accelRel * runMin ? accelAbs : accelRel * runMin;
        if (speed >= runMin + rise)
            event = true; // re-acceleration — a freewheel never speeds up

        // ANTI-MOTOR "not freewheeling": the motor only ever drives FORWARD, so a platter
        // moving BACKWARD can't happen without a hand — except the brief backward coast
        // right after a backspin release, which freewheels (decel ≈ friction) toward 0.
        // So while moving backward above the floor, hold UNLESS the deceleration matches a
        // freewheel. This catches the steady/braked stretches of a pull (the pull-back
        // stutter) that carry no re-acceleration to detect — WITHOUT touching forward
        // releases, which coast WITH the motor and stay snappy via releaseHold.
        // `decel` = deceleration magnitude (>0 = slowing toward 0).
        const double decel = -accHat * (vHat > 0.0 ? 1.0 : -1.0);
        if (vHat < 0.0 && speed >= sustainSpeed && std::abs (decel - freewheelDecel) > decelTol * freewheelDecel)
            event = true;

        if (event)
        {
            runMin = speed;
            if (engaged || speed >= captureFloor)
                holdLeft = handHold;
        }
        if (holdLeft > 0.0)
            holdLeft -= dt;

        if (engaged)
        {
            if (offT <= releaseHold || holdLeft > 0.0)
            {
                bridging = true;
                return Mode::Scratch; // flicker-hold, or hand evidence still fresh
            }
            engaged = false;
            bridging = false;
            return Mode::Released; // no evidence of a hand — the motor takes it
        }

        // Already released: strong evidence re-captures (a stroke with the cap dead).
        if (holdLeft > 0.0)
        {
            engaged = true;
            bridging = true;
            return Mode::Scratch;
        }
        return Mode::Released;
    }

private:
    double rate = 44100.0;
    double vHat = 0.0;     // smoothed platter velocity (counts/s) — the working signal
    double vFast = 0.0;    // leading velocity estimate (seeds vHat at the drop)
    double accHat = 0.0;   // smoothed platter acceleration (counts/s²) — for the hard-brake test
    double runMin = -1.0;  // running minimum of |vHat| since the drop / last event (-1 = unset)
    double holdLeft = 0.0; // seconds of hand-evidence hold remaining
    double offT = 0.0;     // seconds since the cap dropped
    int    lastDir = 0;    // last confident direction of motion (for reversal detection)
    bool   wasOff = false; // was the cap already off last block (detects the drop transition)
    bool   bridging = false;
    bool   engaged = false;
};
