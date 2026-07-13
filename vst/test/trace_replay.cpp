// Replay a captured trace (make trace) through the CURRENT TouchGate and re-emit the
// CSV with the gate's decision (mode/engaged) and the pitch recomputed — so a fix can
// be measured against real hardware data before touching the plugin. Motor state is
// taken from the capture (it doesn't depend on the gate); the playhead column is
// carried over unchanged (stale).
//
//   clang++ -std=c++17 -I../src trace_replay.cpp -o trace_replay
//   ./trace_replay in.csv out.csv [handHoldSec] [releaseHoldSec] [velocity|servo|classic]
//   python3 trace_analyze.py out.csv
//
// The pitch is recomputed with the jog→pitch model the engine would use: velocity
// mode by default, `servo` for the position servo, or `classic` for the
// counts-per-block path (mirror of SC1000_SCRATCH_MODE) — replay a capture each
// way to A/B a feel change offline.
#include "TouchGate.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>

int main(int argc, char** argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: trace_replay in.csv out.csv [handHoldSec] [releaseHoldSec] [velocity|servo|classic]\n"); return 2; }
    FILE* in = std::fopen(argv[1], "r");
    if (!in) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    FILE* out = std::fopen(argv[2], "w");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 2; }

    TouchGate g;
    enum class Mode { Servo, Velocity, Classic };
    Mode mode = Mode::Velocity;
    // Positional numbers = handHold then releaseHold; the words velocity/servo/
    // classic pick the jog→pitch model anywhere after the two paths.
    for (int i = 3, pos = 0; i < argc; ++i)
    {
        const std::string a = argv[i];
        if      (a == "velocity") mode = Mode::Velocity;
        else if (a == "servo")    mode = Mode::Servo;
        else if (a == "classic")  mode = Mode::Classic;
        else if (pos == 0) { g.handHold    = std::stod(a); ++pos; }
        else if (pos == 1) { g.releaseHold = std::stod(a); ++pos; }
        else { std::fprintf(stderr, "unexpected arg: %s\n", a.c_str()); return 2; }
    }

    // Engine pitch constants (mirror ScratchEngine.h) so the recomputed pitch matches
    // the plugin — lets the analyzer's [R]/[2] (pitch-based) score the replayed gate.
    constexpr double kPlat = 2275.0, kScratchTau = 0.020, kSlipTau = 0.035;
    constexpr double kServoVelTau = 0.060, kServoCatch = 0.040, kServoErrMax = 0.150;
    constexpr double kServoTickGap = 0.0106, kServoAuthTau = 0.030;
    constexpr double kServoAuth = 100.0, kServoAuthNow = 375.0;
    constexpr double kVelTau = 0.020;
    double pitch = 0.0;
    double servoErr = 0.0, servoV = 0.0, servoHandV = 0.0, jogQuietT = 1.0, velV = 0.0;
    bool servoEngaged = false;

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
        int rawJog = std::stoi(c[2]); int jog = std::stoi(c[3]); int rawTouch = std::stoi(c[4]);
        double motorSpeed = std::stod(c[6]); int motorStopped = std::stoi(c[7]);
        (void) motorStopped;
        if (prevT >= 0 && t > prevT) rate = prevN / (t - prevT);
        prevT = t; prevN = n;
        g.configure(rate);
        const double dt = (double) n / rate;
        // Servo deadband variant: eat only a time-isolated ±1 (ScratchEngine.h).
        const int jogServo = (std::abs(rawJog) <= 1 && jogQuietT >= kServoTickGap) ? 0 : rawJog;
        jogQuietT = (rawJog != 0) ? 0.0 : jogQuietT + dt;
        servoHandV += (1.0 - std::exp(-dt / kServoAuthTau)) * ((double) jogServo / dt - servoHandV);
        velV += (1.0 - std::exp(-dt / kVelTau)) * ((double) jog / (dt * kPlat) - velV);
        auto m = g.process(n, rawTouch != 0, jog, motorSpeed);
        // recompute pitch with the same model the engine uses
        const bool servoScratch = mode == Mode::Servo && rawTouch != 0 && m == TouchGate::Mode::Scratch;
        if (servoScratch && !servoEngaged) { servoErr = 0.0; servoV = pitch; }
        servoEngaged = servoScratch;
        double target, tau;
        if (servoScratch)
        {
            const double hand = (double) jogServo / kPlat;
            servoErr = std::fmin(kServoErrMax, std::fmax(-kServoErrMax, servoErr + hand));
            servoV += (1.0 - std::exp(-dt / kServoVelTau)) * (hand / dt - servoV);
            target = servoV + servoErr / kServoCatch;
            // Slipmat write-off (mirror of ScratchEngine.h): never reverse against the hand;
            // authority = one strong block (counts/s) or the pure-hand velocity EMA.
            const double handNow = (double) jogServo / dt;
            const int recDir  = (pitch > 0.0) - (pitch < 0.0);
            const int handDir = (std::abs(handNow) >= kServoAuthNow)
                                    ? ((handNow > 0.0) - (handNow < 0.0))
                                    : ((servoHandV > kServoAuth) - (servoHandV < -kServoAuth));
            if (recDir != 0 && handDir != -recDir && target * (double) recDir < 0.0)
            {
                target = 0.0;
                servoErr = (0.0 - servoV) * kServoCatch;
            }
            tau = kScratchTau;
        }
        else if (m == TouchGate::Mode::Scratch && mode == Mode::Velocity)
                                                  { target = velV; tau = kScratchTau; }
        else if (m == TouchGate::Mode::Scratch)   { target = (n > 0) ? (double) jog * rate / (kPlat * n) : 0.0; tau = kScratchTau; }
        else if (m == TouchGate::Mode::Coast)     { target = pitch; tau = kScratchTau; }
        else                                      { target = motorSpeed; tau = kSlipTau; }
        const double pitchOld = pitch;
        pitch += (1.0 - std::exp(-(double) n / (tau * rate))) * (target - pitch);
        if (servoScratch) servoErr -= 0.5 * (pitchOld + pitch) * dt;
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
