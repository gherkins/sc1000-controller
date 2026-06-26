#pragma once

#include <vector>
#include <string>
#include <cstdio>

// Debug capture: one fixed-size record per audio block, holding the firmware MIDI
// stream as decoded (touch bit + jog delta + transport) right next to the plugin's
// own decision for that block (gate mode, motor, pitch, playhead). Because both
// live in one file on one clock, the firmware stream and the plugin's behaviour are
// already time-aligned — no cross-correlation of two logs.
//
// Realtime-safe: enable() pre-reserves the row buffer off the audio thread; push()
// only appends within that reserved capacity (no alloc, no lock); dump() writes the
// CSV off the audio thread (on teardown). Disabled by default — zero cost beyond a
// bool test. Turn on with the env var SC1000_TRACE=/path/to/trace.csv (read by the
// processor). See docs/ARCHITECTURE.md "Touch sensing".
struct TraceLog
{
    struct Row
    {
        double t;            // seconds since the stream started
        int    n;            // block size (samples)
        int    rawJog;       // CC20 delta consumed this block (pre-deadband)
        int    jog;          // after the idle-trickle deadband
        int    rawTouch;     // CC21 / Note20 cap bit, as the firmware sent it
        int    playing;      // transport (Start/Stop)
        double motorSpeed;   // virtual motor (1 playing, brakes to 0)
        int    motorStopped; // motor not driving the platter
        int    mode;         // gate: 0 Released, 1 Scratch, 2 Coast
        int    engaged;      // gate engaged (platter follows the jog)
        double pitch;        // playback rate (1× units)
        double playheadSec;  // sample position
    };

    void enable (const std::string& path, double sampleRate, double seconds = 900.0)
    {
        filePath = path;
        rate     = sampleRate > 0.0 ? sampleRate : 44100.0;
        sampleClock = 0;
        rows.clear();
        rows.reserve ((size_t) (seconds * 400.0)); // generous upper bound (~400 blocks/s)
        on = true;
    }

    bool enabled() const noexcept { return on; }

    // Advance the sample clock for this block and return its start time (seconds).
    double stamp (int n) noexcept { const double t = (double) sampleClock / rate; sampleClock += n; return t; }

    // Audio thread — realtime-safe while within the reserved capacity.
    void push (const Row& r)
    {
        if (on && rows.size() < rows.capacity())
            rows.push_back (r);
    }

    // Off the audio thread (teardown). Writes the CSV.
    void dump()
    {
        if (! on || filePath.empty())
            return;
        if (FILE* f = std::fopen (filePath.c_str(), "w"))
        {
            std::fprintf (f, "t,n,rawJog,jog,rawTouch,playing,motorSpeed,motorStopped,mode,engaged,pitch,playheadSec\n");
            for (const auto& r : rows)
                std::fprintf (f, "%.5f,%d,%d,%d,%d,%d,%.4f,%d,%d,%d,%.5f,%.5f\n",
                              r.t, r.n, r.rawJog, r.jog, r.rawTouch, r.playing, r.motorSpeed,
                              r.motorStopped, r.mode, r.engaged, r.pitch, r.playheadSec);
            std::fclose (f);
        }
    }

private:
    bool        on = false;
    std::string filePath;
    double      rate = 44100.0;
    long long   sampleClock = 0;
    std::vector<Row> rows;
};
