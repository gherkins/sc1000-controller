#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include <memory>

#include "PluginProcessor.h"
#include "RenoiseLookAndFeel.h"

// Waveform + playhead, styled like Renoise's Sampler/Waveform editor: a light-grey
// panel with a black waveform trace and an orange playhead marker. One of the two
// headline visual cues the hardware lacks.
class WaveformDisplay : public juce::Component
{
public:
    explicit WaveformDisplay (ScratchAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (renoise::waveBg);          // light Renoise sample-editor field
        g.fillRect (r);
        g.setColour (renoise::border);
        g.drawRect (r, 1);

        auto inner = r.reduced (2);
        auto& thumb = proc.getThumbnail();

        if (thumb.getTotalLength() > 0.0)
        {
            g.setColour (renoise::waveTrace);   // black waveform
            thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), 0.95f);

            const double len = proc.getLengthSeconds();
            if (len > 0.0)
            {
                const double t = juce::jlimit (0.0, len, proc.getPlayheadSeconds());
                const float x = (float) inner.getX()
                              + (float) (t / len * (double) inner.getWidth());

                g.setColour (renoise::accent);  // orange playhead
                g.fillRect (x - 0.75f, (float) inner.getY(), 1.5f, (float) inner.getHeight());

                juce::Path tab;                 // Renoise-style marker tab at the top
                tab.addTriangle (x - 5.0f, (float) inner.getY(),
                                 x + 5.0f, (float) inner.getY(),
                                 x,        (float) inner.getY() + 6.0f);
                g.fillPath (tab);
            }
        }
        else
        {
            g.setColour (juce::Colour (0xff77756f));
            g.drawText ("drop an audio file here", r, juce::Justification::centred);
        }
    }

private:
    ScratchAudioProcessor& proc;
};

// The deck face — a 1:1 scale model of the SC1000's front, reproduced from photo
// measurements so the proportions, pad sizes and fader head match the hardware.
// Everything is positioned as a fraction of the device body (measured W:H = 0.772),
// which is letterboxed inside this component. Same dark Renoise palette — only the
// geometry mirrors the device:
//   • big central jog platter (orange DJ marker spins with the playhead)
//   • four CUE pads in the corners (CUE 3/4 top, CUE 1/2 bottom)
//   • horizontal crossfader at the bottom with the tall protruding fader head
class DeckFace : public juce::Component
{
public:
    explicit DeckFace (ScratchAudioProcessor& p) : proc (p) {}

    // Smoothed fader-head position (0..1), pushed each tick by the editor's timer so
    // the head glides over slow-speed 7-bit/ADC jitter without lagging real moves.
    void setHeadDisplay (float pos) noexcept { headDisplay = juce::jlimit (0.0f, 1.0f, pos); }

    // Debounced finger-on-platter flag, pushed each tick by the editor's timer so the
    // touch ring rides capacitive dropouts (brief note-off flicker) without blinking.
    void setTouchDisplay (bool on) noexcept { touchDisplay = on; }

    // --- device geometry, as fractions of the body (from the product photo) ---
    static constexpr float kBodyRatio  = 0.772f; // body width / height
    static constexpr float kPlatterCx  = 0.500f; // platter centre x
    static constexpr float kPlatterCy  = 0.395f; // platter centre y
    static constexpr float kPlatterDia = 0.930f; // platter diameter / body width
    static constexpr float kPadSize    = 0.092f; // pad side / body width
    static constexpr float kPadLeftX   = 0.0676f, kPadRightX = 0.8637f; // pad left edges
    static constexpr float kPadTopY    = 0.0214f, kPadBotY   = 0.6938f; // pad top edges
    static constexpr float kFaderCy    = 0.892f; // crossfader centre y
    static constexpr float kFaderTrack = 0.451f; // track width / body width
    static constexpr float kHeadW      = 0.092f; // fader head width / body width
    static constexpr float kHeadH      = 0.119f; // fader head height / body height

    void paint (juce::Graphics& g) override
    {
        // fit the device body (kBodyRatio) inside our bounds, centered
        auto area = getLocalBounds().toFloat();
        float bw = area.getWidth();
        float bh = bw / kBodyRatio;
        if (bh > area.getHeight()) { bh = area.getHeight(); bw = bh * kBodyRatio; }
        const float bx = area.getCentreX() - bw * 0.5f;
        const float by = area.getCentreY() - bh * 0.5f;

        auto fx = [&] (float f) { return bx + f * bw; };
        auto fy = [&] (float f) { return by + f * bh; };

        drawPlatter (g, fx (kPlatterCx), fy (kPlatterCy), kPlatterDia * bw * 0.5f);

        const float cs = kPadSize * bw;
        // cue pad → crossfader mode (faderMode): cue 1=pitch 2=volume 3=curve 4=brake
        drawCue (g, fx (kPadLeftX),  fy (kPadTopY), cs, "CURVE", 2);
        drawCue (g, fx (kPadRightX), fy (kPadTopY), cs, "BRAKE", 3);
        drawCue (g, fx (kPadLeftX),  fy (kPadBotY), cs, "PITCH", 0);
        drawCue (g, fx (kPadRightX), fy (kPadBotY), cs, "VOL",   1);

        drawCrossfader (g, bx, by, bw, bh);
        drawModeReadout (g);
    }

private:
    void drawPlatter (juce::Graphics& g, float cx, float cy, float r)
    {
        const float d = r * 2.0f;
        juce::ColourGradient grad (juce::Colour (0xff2a2a2a), cx, cy,
                                   juce::Colour (0xff141414), cx, cy - r, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - r, cy - r, d, d);

        g.setColour (juce::Colour (0xff202020));           // grooves
        for (float gr = r * 0.42f; gr < r * 0.97f; gr += 6.0f)
            g.drawEllipse (cx - gr, cy - gr, gr * 2.0f, gr * 2.0f, 1.0f);

        g.setColour (renoise::edgeLight.withAlpha (0.4f)); // rim
        g.drawEllipse (cx - r, cy - r, d, d, 1.5f);

        const float lr = r * 0.30f;                        // centre label — fills orange while playing
        const bool playing = proc.getControlState().playing.load();
        g.setColour (playing ? renoise::accent : juce::Colour (0xff181818));
        g.fillEllipse (cx - lr, cy - lr, lr * 2.0f, lr * 2.0f);
        g.setColour (renoise::accent.withAlpha (playing ? 1.0f : 0.85f));
        g.drawEllipse (cx - lr, cy - lr, lr * 2.0f, lr * 2.0f, 1.5f);
        g.setColour (juce::Colours::black);                // spindle hole
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);

        // marker: 0 at top. Accumulate playhead deltas (≈ 33 rpm visual) and phase-unwrap
        // the loop boundary so the marker keeps spinning instead of snapping back to 0 when
        // playback wraps end→start. Sub-loop jumps (scratches) still rotate the marker 1:1.
        constexpr double secsPerRev = 1.8;
        constexpr double twoPi      = juce::MathConstants<double>::twoPi;
        const double now = proc.getPlayheadSeconds();
        const double len = proc.getLengthSeconds();
        double delta = now - lastPlayheadSeconds;
        if (len > 0.0)
            delta -= len * std::round (delta / len);  // unwrap end→start wrap to the short way round
        markerAngle = std::fmod (markerAngle + delta * (twoPi / secsPerRev), twoPi);
        lastPlayheadSeconds = now;
        const float ang = (float) markerAngle - juce::MathConstants<float>::halfPi;
        const float ix = cx + std::cos (ang) * lr;
        const float iy = cy + std::sin (ang) * lr;
        const float ox = cx + std::cos (ang) * r * 0.9f;
        const float oy = cy + std::sin (ang) * r * 0.9f;
        g.setColour (renoise::accent);
        g.drawLine (ix, iy, ox, oy, 2.5f);
        const float dot = 5.0f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (ox - dot, oy - dot, dot * 2.0f, dot * 2.0f);
        g.setColour (renoise::accent);
        g.drawEllipse (ox - dot, oy - dot, dot * 2.0f, dot * 2.0f, 1.5f);

        if (touchDisplay)                                  // finger-on-platter ring (debounced)
        {
            g.setColour (renoise::accent);
            g.drawEllipse (cx - r, cy - r, d, d, 3.0f);
        }
    }

    // A cue pad. `mode` is the crossfader mode it selects (0-3); the active one
    // gets a mode-coloured border so you can see the current selection at a glance
    // (inner fill and label stay default).
    void drawCue (juce::Graphics& g, float x, float y, float s, const juce::String& label, int mode)
    {
        const bool active = (proc.getControlState().faderMode.load() == mode);

        juce::Rectangle<float> sq (x, y, s, s);
        g.setGradientFill (juce::ColourGradient (renoise::buttonTop, sq.getX(), sq.getY(),
                                                 renoise::buttonBot, sq.getX(), sq.getBottom(), false));
        g.fillRoundedRectangle (sq, 3.0f);
        g.setColour (renoise::accent.withAlpha (0.16f));    // faint lit centre (default)
        g.fillRoundedRectangle (sq.reduced (s * 0.16f), 2.0f);
        g.setColour (active ? renoise::modeColour (mode) : renoise::accent); // active → mode-coloured border
        g.drawRoundedRectangle (sq, 3.0f, active ? 2.0f : 1.5f);

        g.setColour (renoise::text);                        // label below the pad (default)
        g.setFont (juce::Font (juce::FontOptions (juce::jlimit (8.0f, 12.0f, s * 0.30f), juce::Font::bold)));
        const float lw = juce::jmax (s, 44.0f);
        const juce::Rectangle<float> lr (sq.getCentreX() - lw * 0.5f, sq.getBottom() + 2.0f, lw, 13.0f);
        g.drawText (label, lr, juce::Justification::centred);
    }

    // Crossfader: thin sunken slot + a tall head that protrudes above/below it (as
    // on the device). Shows CC16 live; the engine treats it as a hysteresis CUT
    // (silent only at the far-left edge), so the head is orange while audio passes
    // and dims to grey at the cut.
    void drawCrossfader (juce::Graphics& g, float bx, float by, float bw, float bh)
    {
        const float trackW = kFaderTrack * bw;
        const float tx     = bx + bw * 0.5f - trackW * 0.5f;
        const float ty     = by + kFaderCy * bh;
        const float slotH  = juce::jmax (6.0f, 0.022f * bh);

        juce::Rectangle<float> slot (tx, ty - slotH * 0.5f, trackW, slotH);
        g.setColour (renoise::inset);
        g.fillRoundedRectangle (slot, 3.0f);
        g.setColour (renoise::border);
        g.drawRoundedRectangle (slot, 3.0f, 1.0f);

        // Head follows the live physical fader (faderRaw) even while Shift freezes the
        // cut. Its colour stays neutral (orange) at rest and takes the active mode's
        // colour only while Shift is held (you're dialing that param). The cut/pass
        // state is not colour-coded.
        auto&       cstate  = proc.getControlState();
        const float v       = headDisplay; // smoothed by the editor timer (see setHeadDisplay)
        const bool  shifted = cstate.shiftHeld.load();

        const float headW  = kHeadW * bw;
        const float headH  = kHeadH * bh;
        const float travel = trackW - headW;
        juce::Rectangle<float> head (tx + travel * v, ty - headH * 0.5f, headW, headH);

        const juce::Colour top = shifted ? renoise::modeColour (cstate.faderMode.load()) : renoise::accent;
        const juce::Colour bot = shifted ? top.darker (0.4f) : renoise::accentDim;
        g.setGradientFill (juce::ColourGradient (top, head.getX(), head.getY(),
                                                 bot, head.getX(), head.getBottom(), false));
        g.fillRoundedRectangle (head, 3.0f);
        g.setColour (renoise::border);
        g.drawRoundedRectangle (head, 3.0f, 1.0f);

        g.setColour (renoise::border.withAlpha (0.6f));     // centre grip line
        g.drawLine (head.getCentreX(), head.getY() + 4.0f, head.getCentreX(), head.getBottom() - 4.0f, 1.5f);
    }

    // Bottom-right panel: all four shift-layer values, always shown. The active
    // mode (faderMode) is marked with a caret and drawn in its mode colour; the
    // rest are dimmed. Label left-aligned, value right-aligned per row.
    void drawModeReadout (juce::Graphics& g)
    {
        auto& cs = proc.getControlState();
        const int active = cs.faderMode.load();

        const int   pct = juce::roundToInt ((cs.pitchScale.load() - 1.0f) * 100.0f);
        const float cv  = cs.faderCurve.load();
        const juce::String mul (juce::CharPointer_UTF8 ("\xc3\x97")); // ×
        const juce::String labels[4] = { "PITCH", "VOL", "CURVE", "BRAKE" };
        const juce::String vals[4] = {
            juce::String (pct > 0 ? "+" : "") + juce::String (pct) + "%",
            juce::String (juce::roundToInt (cs.volA.load() * 100.0f)) + "%",
            juce::String (cv < 0.34f ? "sharp" : cv < 0.67f ? "mid" : "soft"),
            juce::String (cs.brakeScale.load(), 1) + mul
        };

        const float lh = 14.0f, w = 96.0f;
        juce::Rectangle<float> box ((float) getWidth() - w - 6.0f,
                                    (float) getHeight() - lh * 4.0f - 6.0f, w, lh * 4.0f);
        g.setColour (renoise::bg.withAlpha (0.62f));        // faint backing for legibility
        g.fillRoundedRectangle (box.expanded (4.0f, 3.0f), 3.0f);

        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::plain)));
        for (int i = 0; i < 4; ++i)
        {
            const bool on = (i == active);
            juce::Rectangle<float> row (box.getX(), box.getY() + (float) i * lh, box.getWidth(), lh);
            g.setColour (on ? renoise::modeColour (i) : renoise::textDim);
            g.drawText (on ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb8")) : juce::String(),
                        row.removeFromLeft (12.0f), juce::Justification::centredLeft);
            g.drawText (labels[i], row, juce::Justification::centredLeft);
            g.drawText (vals[i],   row, juce::Justification::centredRight);
        }
    }

    float  headDisplay         = 0.5f;  // smoothed fader-head position (matches faderRaw default: centre)
    bool   touchDisplay        = false; // debounced finger-on-platter state (set by the editor timer)
    double markerAngle         = 0.0;   // accumulated platter rotation (rad) — spins continuously across loops
    double lastPlayheadSeconds = 0.0;   // previous playhead sample, for delta accumulation
    ScratchAudioProcessor& proc;
};

class ScratchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::FileDragAndDropTarget,
                                    private juce::Timer
{
public:
    explicit ScratchAudioProcessorEditor (ScratchAudioProcessor&);
    ~ScratchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void openFileChooser();

    ScratchAudioProcessor& processor;
    RenoiseLookAndFeel lnf;

    juce::Label      headerLabel;
    WaveformDisplay  waveform;
    DeckFace         deck;
    juce::TextButton loadButton { "Load sample" };
    juce::Label      infoLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerBounds;
    float faderDisplay = 1.0f; // smoothed fader-head position fed to the deck
    int   touchHoldTicks = 0;  // debounce: ticks left to keep the touch ring lit after release

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchAudioProcessorEditor)
};
