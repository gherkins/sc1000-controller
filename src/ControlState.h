#pragma once

#include <atomic>

// Lock-free control state shared between the MIDI/message thread (producer) and
// the audio thread (consumer). One writer side, one reader side per field — plain
// atomics are sufficient. See docs/MIDI-MAPPING.md for the device contract.
struct ControlState
{
    // Crossfader (CC16): 0..1. Default fully open so audio passes before it's touched.
    std::atomic<float> crossfader { 1.0f };

    // Volume pots (CC18 / CC19): 0..1. volA drives master out for the MVP; volB is
    // spare (only one voice today — see ARCHITECTURE.md open question 5).
    std::atomic<float> volA { 1.0f };
    std::atomic<float> volB { 1.0f };

    // Jog wheel (CC20): signed counts accumulated since the audio thread last read.
    std::atomic<int> jogAccum { 0 };

    // Jog touch (Note20): finger on the platter.
    std::atomic<bool> touched { false };

    // Transport: continuous 1× playback on/off (toggled by the cue / Start buttons).
    std::atomic<bool> playing { false };

    // Producer: add a decoded signed jog delta.
    void addJog (int delta) noexcept { jogAccum.fetch_add (delta, std::memory_order_relaxed); }

    // Consumer (audio thread): take and reset the accumulated counts.
    int consumeJog() noexcept { return jogAccum.exchange (0, std::memory_order_relaxed); }

    // Producer: flip play/stop (called on a cue / Start button press).
    void togglePlaying() noexcept
    {
        playing.store (! playing.load (std::memory_order_relaxed), std::memory_order_relaxed);
    }
};
