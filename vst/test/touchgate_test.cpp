// Standalone unit test for TouchGate (no JUCE, no DAW).
//   clang++ -std=c++17 -I../src touchgate_test.cpp -o touchgate_test && ./touchgate_test
// or, from the repo root:  make touchtest
//
// "Stick slipmat": follow the jog only while actively scratching (cap on + moving), or
// cue-by-hand at a dead motor. Anything else — a lift, a stop, a coasting platter —
// snaps to the motor; the leftover platter momentum is deliberately NOT followed.

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

    // 5) Stop with the cap still reading ON (it lingers after a lift): a still platter
    //    pulls to the motor — not a dead stop.
    {
        std::printf ("[5] stop with cap lingering ON → pulls to the motor\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);
        expect ("just stopped: holds (Coast)",     g.process (kBlock, true, 0, false), TouchGate::Mode::Coast);
        expect ("cap still on but still → motor",  run (g, 3, true, 0, false),         TouchGate::Mode::Released);
    }

    // 6) Not running (motor stopped) → the jog always scrubs (cue by hand).
    {
        std::printf ("[6] standstill follows the jog\n");
        auto g = makeGate();
        expect ("no touch, motor stopped → scrub", g.process (kBlock, false, 0,  true), TouchGate::Mode::Scratch);
        expect ("cue by hand (jog, no touch)",     g.process (kBlock, false, 20, true), TouchGate::Mode::Scratch);
    }

    // 7) Brief reversal/flicker (1 block) holds (Coast), then resumes — doesn't snap.
    {
        std::printf ("[7] brief reversal holds, then resumes\n");
        auto g = makeGate();
        run (g, 5, true, 20, false);
        expect ("1-block pause holds (Coast)", g.process (kBlock, true, 0, false), TouchGate::Mode::Coast);
        expect ("motion resumes → scratch",    g.process (kBlock, true, 20, false), TouchGate::Mode::Scratch);
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
