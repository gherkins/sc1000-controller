#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Palette sampled from Renoise's default dark theme + its Sampler/Waveform editor.
// (Renoise themes are user-customizable and a plugin can't read the active theme,
// so we match the stock dark+orange look most people run.)
namespace renoise
{
    inline const juce::Colour bg        { 0xff1c1c1c }; // window background
    inline const juce::Colour panel     { 0xff2b2b2b }; // raised panel / header bar
    inline const juce::Colour inset     { 0xff141414 }; // sunken inset
    inline const juce::Colour border    { 0xff0d0d0d };
    inline const juce::Colour edgeLight { 0xff3a3a3a }; // top bevel highlight
    inline const juce::Colour accent    { 0xffe0701c }; // signature orange
    inline const juce::Colour accentDim { 0xffb15a18 };
    inline const juce::Colour text      { 0xffcacaca };
    inline const juce::Colour textDim   { 0xff8a8a8a };
    inline const juce::Colour buttonTop { 0xff3c3c3c };
    inline const juce::Colour buttonBot { 0xff2a2a2a };

    // Renoise sample-editor waveform panel: black trace on a light-grey field.
    inline const juce::Colour waveBg    { 0xffd4d4d2 };
    inline const juce::Colour waveTrace { 0xff111111 };
    inline const juce::Colour waveMid   { 0xffb9b9b6 }; // faint centre line

    // Shift-layer crossfader modes (cue 1-4): one colour per role, used to tint the
    // active cue pad and the fader head while Shift is held.
    inline const juce::Colour modePitch  { 0xff3d8bff }; // cue 1: pitch  — blue
    inline const juce::Colour modeVolume { 0xff35c46a }; // cue 2: volume — green
    inline const juce::Colour modeCurve  { 0xffc861ff }; // cue 3: curve  — magenta
    inline const juce::Colour modeBrake  { 0xffe23b3b }; // cue 4: brake  — red

    inline juce::Colour modeColour (int mode)
    {
        switch (mode)
        {
            case 0:  return modePitch;
            case 1:  return modeVolume;
            case 2:  return modeCurve;
            case 3:  return modeBrake;
            default: return accent;
        }
    }
}

// Flat, beveled, dark buttons with orange when pressed — Renoise's toolbar feel.
class RenoiseLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RenoiseLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, renoise::bg);
        setColour (juce::TextButton::buttonColourId,   renoise::buttonBot);
        setColour (juce::TextButton::textColourOffId,  renoise::text);
        setColour (juce::TextButton::textColourOnId,   renoise::accent);
        setColour (juce::Label::textColourId,          renoise::textDim);
        setColour (juce::PopupMenu::backgroundColourId, renoise::panel);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        juce::Colour top = renoise::buttonTop, bot = renoise::buttonBot;
        if (down)       { top = renoise::accent;  bot = renoise::accentDim; }
        else if (over)  { top = top.brighter (0.08f); bot = bot.brighter (0.08f); }

        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(),
                                                 bot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (renoise::border);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (renoise::edgeLight.withAlpha (down ? 0.0f : 0.5f)); // top bevel
        g.drawLine (r.getX() + 3.0f, r.getY() + 1.0f, r.getRight() - 3.0f, r.getY() + 1.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions ((float) juce::jmin (15, buttonHeight - 8)));
    }
};
