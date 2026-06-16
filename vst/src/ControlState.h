#pragma once

#include <atomic>

// Lock-free control state shared between the MIDI/message thread (producer) and
// the audio thread (consumer). One writer side, one reader side per field — plain
// atomics are sufficient. See docs/MIDI-MAPPING.md for the device contract.
struct ControlState
{
    // Crossfader (CC16): 0..1. Default fully open so audio passes before it's touched.
    // While Shift is held this freezes (the cut holds) and the fader drives a param
    // instead; faderRaw still tracks the live physical position for the GUI head.
    std::atomic<float> crossfader { 1.0f };
    std::atomic<float> faderRaw   { 1.0f };

    // Volume pots (CC18 / CC19): 0..1. volA drives master out for the MVP; volB is
    // spare (only one voice today — see ARCHITECTURE.md open question 5).
    std::atomic<float> volA { 1.0f };
    std::atomic<float> volB { 1.0f };

    // Jog wheel (CC20): signed counts accumulated since the audio thread last read.
    std::atomic<int> jogAccum { 0 };

    // Jog touch (Note20): finger on the platter.
    std::atomic<bool> touched { false };

    // Transport: continuous 1× playback on/off (toggled by the Start/Stop button).
    std::atomic<bool> playing { false };

    // --- Shift-layer crossfader modes (see docs/MIDI-MAPPING.md) ---
    // While Shift (Note 25) is held the crossfader stops cutting and instead dials
    // one of these parameters; which one is picked by the cue pads (Notes 32-35).
    // The MIDI thread writes them; the audio thread / GUI read them.
    std::atomic<bool> shiftHeld { false }; // Shift (Note 25) currently held
    std::atomic<int>  faderMode { 0 };     // 0=pitch 1=volume 2=curve 3=brake (cue 1-4)

    // Parameters the shifted crossfader writes (engine reads them each block):
    std::atomic<float> pitchScale { 1.0f }; // varispeed multiplier on playback rate (1.0 = unity)
    std::atomic<float> faderCurve { 0.0f }; // crossfader cut curve: 0 = sharp/cut … 1 = soft/fade
    std::atomic<float> brakeScale { 1.0f }; // multiplies the tape-stop brake length (1.0 = stock)
    // (Volume mode writes the existing volA — no separate field.)

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
