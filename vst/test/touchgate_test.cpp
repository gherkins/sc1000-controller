// Standalone unit test for TouchGate (no JUCE, no DAW).
//   clang++ -std=c++17 -I../src touchgate_test.cpp -o touchgate_test && ./touchgate_test
// or, from the repo root:  make touchtest
//
// Strict hand-evidence model (2026-07-07, session 6): cap-off RELEASES fast — always —
// unless the platter ACCELERATES or REVERSES, which a freewheel cannot do and so proves
// a hand is on it. Movement alone is never touch; only unfakeable motion is.

#include "TouchGate.h"
#include "TraceLog.h"
#include <cstdio>
#include <cmath>
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

static void expectTrue (const std::string& what, bool ok)
{
    if (! ok) { std::printf ("  FAIL: %s\n", what.c_str()); ++failures; }
    else      { std::printf ("  ok:   %s\n", what.c_str()); }
}

// 48 kHz, 512-sample blocks (≈10.7 ms).
static constexpr double kRate  = 48000.0;
static constexpr int    kBlock = 512;
static constexpr double kDt    = (double) kBlock / kRate;

static TouchGate makeGate()
{
    TouchGate g;
    g.configure (kRate);
    g.reset();
    return g;
}

static TouchGate::Mode run (TouchGate& g, int blocks, bool touch, int jog, double motor)
{
    TouchGate::Mode m = TouchGate::Mode::Released;
    for (int i = 0; i < blocks; ++i)
        m = g.process (kBlock, touch, jog, motor);
    return m;
}

int main()
{
    // 1) Active scratching (cap on + moving) → follow the jog.
    {
        std::printf ("[1] active scratch (cap on + moving)\n");
        auto g = makeGate();
        expect ("first block follows", g.process (kBlock, true, 20, 1.0), TouchGate::Mode::Scratch);
        expect ("stays following",     run (g, 50, true, 20, 1.0),        TouchGate::Mode::Scratch);
    }

    // 2) Grab + back-spin (cap on + motion) → instant catch.
    {
        std::printf ("[2] grab + back-spin\n");
        auto g = makeGate();
        run (g, 20, false, 0, 1.0);
        expect ("catches on the grab", g.process (kBlock, true, -20, 1.0), TouchGate::Mode::Scratch);
    }

    // 3) Slow scratch (cap on + weak motion) → still follow.
    {
        std::printf ("[3] slow scratch (cap on + weak jog) follows\n");
        auto g = makeGate();
        expect ("weak jog with finger follows", run (g, 20, true, 3, 1.0), TouchGate::Mode::Scratch);
    }

    // 4) RELEASE at speed with NO further acceleration (constant then coasting jog):
    //    a freewheel doesn't speed up → nothing negates the drop → commits fast.
    {
        std::printf ("[4] release at ~1x, real freewheel decay → commits fast\n");
        auto g = makeGate();
        run (g, 40, true, 24, 1.0);            // steady stroke ≈1.05×, cap on
        // cap drops; the platter now freewheels at the friction rate (the ONLY decel a
        // release makes) — the gate must recognise it and commit, not read it as braking.
        int rel = -1;
        double v = 24.0 / kDt;                  // counts/s at the drop
        for (int k = 0; k < 40; ++k)
        {
            const int j = (int) (v * kDt + 0.5);
            if (g.process (kBlock, false, j, 1.0) == TouchGate::Mode::Released) { rel = k; break; }
            v -= g.freewheelDecel * kDt;         // physically-correct free-coast
            if (v < 0.0) v = 0.0;
        }
        expectTrue ("released promptly, got block " + std::to_string (rel),
                    rel >= 0 && rel <= (int) (g.releaseHold / kDt) + 6);
        // stays released as the freewheel keeps coasting down at the friction rate
        TouchGate::Mode after = TouchGate::Mode::Released;
        for (int k = 0; k < 8; ++k)
        {
            const int j = (int) (v * kDt + 0.5);
            after = g.process (kBlock, false, j, 1.0);
            v -= g.freewheelDecel * kDt; if (v < 0.0) v = 0.0;
        }
        expect ("stays released", after, TouchGate::Mode::Released);
    }

    // 4b) THE FORWARD-PUSH DROPOUT: cap dies for a long stretch while the hand keeps
    //     DRIVING (a sustained, swelling stroke). Past the convergence grace the repeated
    //     re-accelerations hold (or re-capture) control, so it ends in scratch — the
    //     sample tracks the hand, not the motor. (A brief release at ~releaseHold before
    //     the first post-grace swell is expected and inaudible — motor is same-direction.)
    {
        std::printf ("[4b] long driven cap-dead stroke → ends in scratch (re-capture)\n");
        auto g = makeGate();
        run (g, 20, true, -30, 1.0);
        // a sustained pull that keeps swelling — 30 blocks ≈ 320 ms, well past the grace
        const int seq[] = { -30,-45,-60,-50,-35,-48,-62,-70,-55,-68,-78,-60,-72,-84,-66,
                            -50,-64,-80,-58,-72,-86,-62,-74,-88,-64,-76,-90,-66,-78,-92 };
        TouchGate::Mode m = TouchGate::Mode::Released;
        int scratchBlocks = 0;
        for (int jog : seq) { m = g.process (kBlock, false, jog, 1.0); if (m == TouchGate::Mode::Scratch) ++scratchBlocks; }
        expect ("ends in scratch (hand in control)", m, TouchGate::Mode::Scratch);
        expectTrue ("scratches most of the stroke, got " + std::to_string (scratchBlocks) + "/30", scratchBlocks >= 24);
        expect ("re-touch resumes scratch", g.process (kBlock, true, -40, 1.0), TouchGate::Mode::Scratch);
    }

    // 4c) REVERSAL with the cap off = unfakeable hand evidence → stays scratching.
    {
        std::printf ("[4c] cap-off reversal → stays scratching\n");
        auto g = makeGate();
        run (g, 20, true, 30, 1.0);
        run (g, 2, false, 30, 1.0);                 // coasting forward, cap off
        expect ("reverses through zero → held", run (g, 4, false, -30, 1.0), TouchGate::Mode::Scratch);
    }

    // 4d) Steady drag with cap off, split by DIRECTION (anti-motor test):
    //   • steady BACKWARD pull (jog −20) → HELD: the motor only drives forward, so a
    //     sustained backward platter that isn't freewheeling is a hand (session-8 fix).
    //   • steady FORWARD drag (jog +20) → RELEASED: it coasts WITH the motor, so handing
    //     over is inaudible; releasing keeps forward let-gos snappy (no sag).
    {
        std::printf ("[4d] steady drag: backward held, forward released\n");
        auto gb = makeGate();
        run (gb, 20, true, -20, 1.0);
        expect ("backward steady pull holds", run (gb, 20, false, -20, 1.0), TouchGate::Mode::Scratch);
        auto gf = makeGate();
        run (gf, 20, true, 20, 1.0);
        expect ("forward steady drag releases", run (gf, 20, false, 20, 1.0), TouchGate::Mode::Released);
    }

    // 4d2) A released BACKSPIN coasts backward but FREEWHEELS (decel ≈ friction) → it must
    //      RELEASE (motor takes over), not hold — the anti-motor test only holds backward
    //      motion that ISN'T freewheeling.
    {
        std::printf ("[4d2] backward freewheel (backspin let-go) releases\n");
        auto g = makeGate();
        run (g, 30, true, -40, 1.0);          // backspin, cap on
        int rel = -1;
        double v = -40.0 / kDt;               // counts/s (negative)
        for (int k = 0; k < 40; ++k)
        {
            const int j = (int) (v * kDt - 0.5);
            if (g.process (kBlock, false, j, 1.0) == TouchGate::Mode::Released) { rel = k; break; }
            v += g.freewheelDecel * kDt; if (v > 0.0) v = 0.0; // coast toward 0
        }
        expectTrue ("backspin freewheel releases, got block " + std::to_string (rel), rel >= 0 && rel <= 12);
    }

    // 5) Finger held on the platter with no motion (cap on, jog 0) → holds the record.
    {
        std::printf ("[5] finger down, platter still (cap on) → holds the record\n");
        auto g = makeGate();
        run (g, 5, true, 20, 1.0);
        expect ("just stopped: still follows", g.process (kBlock, true, 0, 1.0), TouchGate::Mode::Scratch);
        expect ("stays held while touched",    run (g, 30, true, 0, 1.0),        TouchGate::Mode::Scratch);
    }

    // 6) Not running (motor stopped) → the jog always scrubs (cue by hand).
    {
        std::printf ("[6] standstill follows the jog\n");
        auto g = makeGate();
        expect ("no touch, motor stopped → scrub", g.process (kBlock, false, 0,  0.0), TouchGate::Mode::Scratch);
        expect ("cue by hand (jog, no touch)",     g.process (kBlock, false, 20, 0.0), TouchGate::Mode::Scratch);
    }

    // 7) Brief reversal/pause WHILE TOUCHED stays in control.
    {
        std::printf ("[7] touched reversal stays in control\n");
        auto g = makeGate();
        run (g, 5, true, 20, 1.0);
        expect ("1-block pause still follows", g.process (kBlock, true, 0,   1.0), TouchGate::Mode::Scratch);
        expect ("reverses → still follows",    g.process (kBlock, true, -20, 1.0), TouchGate::Mode::Scratch);
    }

    // 7b) A brief cap dropout mid-scratch that STILL accelerates rides the hold and
    //     resumes on re-touch (no spurious release).
    {
        std::printf ("[7b] cap flicker with a fresh swell bridges then resumes\n");
        auto g = makeGate();
        run (g, 20, true, 30, 1.0);
        expect ("cap blips off with a speed swell → held", g.process (kBlock, false, 55, 1.0), TouchGate::Mode::Scratch);
        expectTrue ("reported as bridging", g.isBridging());
        expect ("cap returns → scratch again", g.process (kBlock, true, 40, 1.0), TouchGate::Mode::Scratch);
    }

    // 8) THE FORWARD-PUSH DROPOUT: cap fully dead during a stroke. Once released, a
    //    STRONG re-acceleration RE-CAPTURES control so the stroke keeps scratching.
    {
        std::printf ("[8] re-capture from released on a strong cap-dead stroke\n");
        auto g = makeGate();
        run (g, 20, true, 10, 1.0);
        // let it release (steady then still)
        run (g, 20, false, 0, 1.0);
        expectTrue ("released before the stroke", ! g.isEngaged());
        // a fresh forward push, cap still dead: accelerate hard from rest
        TouchGate::Mode m = TouchGate::Mode::Released;
        for (int k = 0; k < 6; ++k)
            m = g.process (kBlock, false, 10 + 15 * k, 1.0);
        expect ("strong accel re-captures", m, TouchGate::Mode::Scratch);
    }

    // 9) TraceLog round-trips a row to CSV.
    {
        std::printf ("[9] TraceLog writes a CSV\n");
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
