// Replay a captured trace (make trace) through the CURRENT TouchGate and re-emit the
// CSV with the gate's decision (mode/engaged) recomputed — so a fix can be measured
// against real hardware data before touching the plugin. Motor state is taken from
// the capture (it doesn't depend on the gate); only the touch decision is re-run.
//
//   clang++ -std=c++17 -I../src trace_replay.cpp -o trace_replay
//   ./trace_replay in.csv out.csv [handHoldSec] [releaseHoldSec]
//   python3 trace_analyze.py out.csv
//
// Pitch/playhead columns are carried over unchanged (stale) — the actionable metric
// is "[1] motor kept running while moving", which is driven by `engaged`.
#include "TouchGate.h"
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: trace_replay in.csv out.csv [releaseHoldSec] [attackConfirmSec]\n"); return 2; }
    FILE* in = std::fopen(argv[1], "r");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    FILE* out = std::fopen(argv[2], "w");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 2; }

    TouchGate g;
    if (argc >= 4) g.handHold    = std::stod(argv[3]);
    if (argc >= 5) g.releaseHold = std::stod(argv[4]);

    // Engine pitch constants (mirror ScratchEngine.h) so the recomputed pitch matches
    // the plugin — lets the analyzer's [R]/[2] (pitch-based) score the replayed gate.
    constexpr double kPlat = 2275.0, kScratchTau = 0.020, kSlipTau = 0.035;
    double pitch = 0.0;

    char line[512];
    bool header = true;
    double rate = 48000.0;
    double prevT = -1.0; int prevN = 0;
    while (std::fgets(line, sizeof line, in))
    {
        if (header) { std::fputs(line, out); header = false; continue; }
        // columns: t,n,rawJog,jog,rawTouch,playing,motorSpeed,motorStopped,mode,engaged,pitch,playheadSec
        std::vector<std::string> c;
        std::stringstream ss(line); std::string tok;
        while (std::getline(ss, tok, ',')) c.push_back(tok);
        if (c.size() < 12) continue;
        double t = std::stod(c[0]); int n = std::stoi(c[1]);
        int jog = std::stoi(c[3]); int rawTouch = std::stoi(c[4]);
        double motorSpeed = std::stod(c[6]); int motorStopped = std::stoi(c[7]);
        if (prevT >= 0 && t > prevT) rate = prevN / (t - prevT);
        prevT = t; prevN = n;
        g.configure(rate);
        auto m = g.process(n, rawTouch != 0, jog, motorSpeed);
        // recompute pitch with the same model the engine uses
        double target, tau;
        if (m == TouchGate::Mode::Scratch)   { target = (n > 0) ? (double) jog * rate / (kPlat * n) : 0.0; tau = kScratchTau; }
        else if (m == TouchGate::Mode::Coast){ target = pitch; tau = kScratchTau; }
        else                                 { target = motorSpeed; tau = kSlipTau; }
        pitch += (1.0 - std::exp(-(double) n / (tau * rate))) * (target - pitch);
        c[8] = std::to_string(m != TouchGate::Mode::Scratch ? 0 : (g.isBridging() ? 2 : 1)); // mode (2 = bridging a cap drop)
        c[9] = std::to_string(g.isEngaged() ? 1 : 0);                 // engaged
        char pbuf[32]; std::snprintf(pbuf, sizeof pbuf, "%.5f", pitch);
        c[10] = pbuf;                                                  // pitch (recomputed)
        for (size_t i = 0; i < c.size(); ++i)
        {
            std::string v = c[i];
            while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
            std::fprintf(out, "%s%s", v.c_str(), i + 1 < c.size() ? "," : "\n");
        }
    }
    std::fclose(in); std::fclose(out);
    return 0;
}
