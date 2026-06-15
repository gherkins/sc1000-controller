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

// Virtual turntable with an orange DJ marker. Rotation follows the playhead, so it
// spins forward/back exactly as you scrub.
class PlatterDisplay : public juce::Component
{
public:
    explicit PlatterDisplay (ScratchAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        const float d  = juce::jmin (b.getWidth(), b.getHeight());
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();
        const float r  = d * 0.5f;

        // vinyl body — subtle radial gradient
        juce::ColourGradient grad (juce::Colour (0xff2a2a2a), cx, cy,
                                   juce::Colour (0xff141414), cx, cy - r, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - r, cy - r, d, d);

        g.setColour (juce::Colour (0xff202020));           // grooves
        for (float gr = r * 0.42f; gr < r * 0.97f; gr += 6.0f)
            g.drawEllipse (cx - gr, cy - gr, gr * 2.0f, gr * 2.0f, 1.0f);

        g.setColour (renoise::edgeLight.withAlpha (0.4f)); // rim
        g.drawEllipse (cx - r, cy - r, d, d, 1.5f);

        const float lr = r * 0.32f;                        // centre label — fills orange while playing
        const bool playing = proc.getControlState().playing.load();
        g.setColour (playing ? renoise::accent : juce::Colour (0xff181818));
        g.fillEllipse (cx - lr, cy - lr, lr * 2.0f, lr * 2.0f);
        g.setColour (renoise::accent.withAlpha (playing ? 1.0f : 0.85f));
        g.drawEllipse (cx - lr, cy - lr, lr * 2.0f, lr * 2.0f, 1.5f);
        g.setColour (juce::Colours::black);                // spindle hole
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5.0f, 5.0f);

        // marker: 0 at top, angle from playhead seconds (≈ 33 rpm visual)
        constexpr double secsPerRev = 1.8;
        const float ang = (float) (proc.getPlayheadSeconds() * juce::MathConstants<double>::twoPi / secsPerRev)
                        - juce::MathConstants<float>::halfPi;
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

        if (proc.getControlState().touched.load())         // finger-on-platter ring
        {
            g.setColour (renoise::accent);
            g.drawEllipse (cx - r, cy - r, d, d, 3.0f);
        }
    }

private:
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
    PlatterDisplay   platter;
    juce::TextButton loadButton { "Load sample" };
    juce::Label      infoLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchAudioProcessorEditor)
};
