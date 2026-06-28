// Standalone unit test for TouchGate (no JUCE, no DAW).
//   clang++ -std=c++17 -I../src touchgate_test.cpp -o touchgate_test && ./touchgate_test
// or, from the repo root:  make touchtest
//
// "Stick slipmat": a finger on the platter (cap on) — or cue-by-hand at a dead motor —
// follows the jog (moving => scratch, still => hold the record under the finger). Only a
// real LIFT (cap off while the motor runs) snaps to the motor; the leftover platter
// momentum is deliberately NOT followed.

#include "TouchGate.h"
#include "TraceLog.h"
#include <cstdio>
#include <string>

static int failures = 0;

static const char* name (TouchGate::Mode m)
{
    return m == TouchGate::Mode::Scratch ? "Scratch"
         : m == TouchGate::Mode::Coast   ? "Coast" : "Released";
}

static void expect (const std::string& what, TouchGate::Mode got, TouchGate::Mode want)
{
    if (got != want) { std::printf ("  FAIL: %s — got %s, want %s\n", what.c_str(), name (got), name (want)); ++failures; }
    else             { std::printf ("  ok:   %s — %s\n", what.c_str(), name (got)); }
}

// 48 kHz, 512-sample blocks (≈10.7 ms). Release hold 20 ms ≈ 2 blocks (Coast on block 1,
// Released by block 2).
static constexpr double kRate  = 48000.0;
static constexpr int    kBlock = 512;

static TouchGate makeGate()
{
    TouchGate g;
    g.releaseHoldSec = 0.020;
    g.configure (kRate);
    g.reset();
    return g;
}

static TouchGate::Mode run (TouchGate& g, int blocks, bool touch, int jog, bool motorStopped)
{
    TouchGate::Mode m = TouchGate::Mode::Released;
    for (int i = 0; i < blocks; ++i)
        m = g.process (kBlock, touch, jog, motorStopped);
    return m;
}

int main()
{
    // 1) Active scratching (cap on + moving) → follow the jog.
    {
        std::printf ("[1] active scratch (cap on + moving)\n");
        auto g = makeGate();
        expect ("first block follows", g.process (kBlock, true, 20, false), TouchGate::Mode::Scratch);
        expect ("stays following",     run (g, 50, true, 20, false),        TouchGate::Mode::Scratch);
    }

    // 2) Grab + back-spin (cap on + motion) → instant catch.
    {
        std::printf ("[2] grab + back-spin\n");
        auto g = makeGate();
        run (g, 20, false, 0, false);
        expect ("catches on the grab", g.process (kBlock, true, -20, false), TouchGate::Mode::Scratch);
    }

    // 3) Slow scratch (cap on + weak motion) → still follow.
    {
        std::printf ("[3] slow scratch (cap on + weak jog) follows\n");
        auto g = makeGate();
        expect ("weak jog with finger follows", run (g, 20, true, 3, false), TouchGate::Mode::Scratch);
    }

    // 4) THE STICK SLIPMAT — release while the platter is still moving FAST (cap off):
    //    must NOT ride the momentum; snaps to the motor.
    {
        std::printf ("[4] release while moving fast (cap off) → snaps to motor, no momentum ride\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);                                    // scratching
        expect ("brief hold (Coast)", g.process (kBlock, false, 20, false), TouchGate::Mode::Coast);
        expect ("then snaps to motor", run (g, 3, false, 20, false),    TouchGate::Mode::Released);
    }

    // 5) Finger held on the platter with no motion (cap on, jog 0): the record holds /
    //    eases to a stop UNDER the finger — it is NOT handed to the motor while touched.
    //    (This is the fix for "pulling back fights the motor": a slow point of a scratch
    //    no longer yanks the playhead to +1×. See docs/ARCHITECTURE.md "Touch sensing".)
    {
        std::printf ("[5] finger down, platter still (cap on) -> holds the record, never fights to the motor\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);
        expect ("just stopped: still follows (hold)", g.process (kBlock, true, 0, false), TouchGate::Mode::Scratch);
        expect ("stays held while touched",           run (g, 30, true, 0, false),        TouchGate::Mode::Scratch);
    }

    // 6) Not running (motor stopped) → the jog always scrubs (cue by hand).
    {
        std::printf ("[6] standstill follows the jog\n");
        auto g = makeGate();
        expect ("no touch, motor stopped → scrub", g.process (kBlock, false, 0,  true), TouchGate::Mode::Scratch);
        expect ("cue by hand (jog, no touch)",     g.process (kBlock, false, 20, true), TouchGate::Mode::Scratch);
    }

    // 7) Brief reversal/pause WHILE TOUCHED stays in control: it follows the jog straight
    //    through zero (no handoff to the motor), so a turnaround doesn't fight.
    {
        std::printf ("[7] touched reversal stays in control (no motor handoff at the turnaround)\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);
        expect ("1-block pause still follows", g.process (kBlock, true, 0,   false), TouchGate::Mode::Scratch);
        expect ("reverses → still follows",    g.process (kBlock, true, -20, false), TouchGate::Mode::Scratch);
    }

    // 7b) A brief cap DROPOUT while still moving (a flicker, not a lift) rides via the
    //     Coast hold and returns to Scratch when the cap comes back — no spurious handoff.
    {
        std::printf ("[7b] brief cap dropout while moving rides (Coast) then resumes scratch\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);
        expect ("cap blips off for 1 block (Coast)", g.process (kBlock, false, 20, false), TouchGate::Mode::Coast);
        expect ("cap returns → scratch again",       g.process (kBlock, true,  20, false), TouchGate::Mode::Scratch);
    }

    // 8) TraceLog round-trips a row to CSV.
    {
        std::printf ("[8] TraceLog writes a CSV\n");
        const char* path = "/tmp/sc1000_tracelog_selftest.csv";
        std::remove (path);
        TraceLog tl;
        tl.enable (path, 48000.0, 1.0);
        tl.push ({ tl.stamp (256), 256, -5, -5, 1, 1, 1.0, 0, 1, 1, -0.6, 0.123 });
        tl.dump();
        bool headerOk = false, rowOk = false;
        if (FILE* f = std::fopen (path, "r"))
        {
            char line[256];
            if (std::fgets (line, sizeof line, f)) headerOk = std::string (line).rfind ("t,n,rawJog", 0) == 0;
            if (std::fgets (line, sizeof line, f)) rowOk = std::string (line).find (",256,-5,-5,1,1,") != std::string::npos;
            std::fclose (f);
        }
        if (! headerOk) { std::printf ("  FAIL: header missing/wrong\n"); ++failures; } else std::printf ("  ok:   header written\n");
        if (! rowOk)    { std::printf ("  FAIL: row missing/wrong\n");    ++failures; } else std::printf ("  ok:   row written\n");
        std::remove (path);
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
