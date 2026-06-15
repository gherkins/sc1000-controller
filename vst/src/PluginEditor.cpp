#include "PluginEditor.h"

ScratchAudioProcessorEditor::ScratchAudioProcessorEditor (ScratchAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      waveform (p),
      deck (p)
{
    setLookAndFeel (&lnf);

    headerLabel.setText ("SC1000", juce::dontSendNotification);
    headerLabel.setJustificationType (juce::Justification::centred);
    headerLabel.setColour (juce::Label::textColourId, renoise::text);
    headerLabel.setColour (juce::Label::backgroundColourId, renoise::panel);
    headerLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (headerLabel);

    addAndMakeVisible (waveform);
    addAndMakeVisible (deck);

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    infoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (infoLabel);

    setSize (420, 670); // sized so the deck draws the device body (W:H 0.772) at full width
    startTimerHz (30);
}

ScratchAudioProcessorEditor::~ScratchAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ScratchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (renoise::bg);

    // thin orange rule under the header bar (Renoise accent)
    g.setColour (renoise::accent);
    g.fillRect (headerBounds.getX(), headerBounds.getBottom() - 1, headerBounds.getWidth(), 1);
}

void ScratchAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    headerBounds = area.removeFromTop (26);
    headerLabel.setBounds (headerBounds);

    auto r = area.reduced (10);

    waveform.setBounds (r.removeFromTop (64));
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (28);
    loadButton.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (8);
    infoLabel.setBounds (bottom);
    r.removeFromBottom (8);

    deck.setBounds (r); // device body: platter + CUE pads + crossfader, by exact proportions
}

void ScratchAudioProcessorEditor::timerCallback()
{
    waveform.repaint();
    deck.repaint();

    auto& cs = processor.getControlState();
    juce::String info;
    info << (cs.playing.load() ? "PLAY   " : "STOP   ");
    if (processor.hasSample())
        info << processor.getSampleName() << "   "
             << juce::String (processor.getPlayheadSeconds(), 2) << " / "
             << juce::String (processor.getLengthSeconds(), 2) << " s   ";
    else
        info << "no sample   ";
    info << "xfade " << juce::String (cs.crossfader.load(), 2)
         << (cs.touched.load() ? "   [touch]" : "");
    infoLabel.setText (info, juce::dontSendNotification);
}

void ScratchAudioProcessorEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Select a sample",
                                                   juce::File(),
                                                   "*.wav;*.aiff;*.aif;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  processor.loadFile (f);
                          });
}

bool ScratchAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".flac")
            || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".ogg"))
            return true;
    return false;
}

void ScratchAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;
    processor.loadFile (juce::File (files[0]));
}
