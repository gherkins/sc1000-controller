#include "PluginEditor.h"

ScratchAudioProcessorEditor::ScratchAudioProcessorEditor (ScratchAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      waveform (p),
      platter (p)
{
    setLookAndFeel (&lnf);

    headerLabel.setText ("SCRATCH", juce::dontSendNotification);
    headerLabel.setJustificationType (juce::Justification::centred);
    headerLabel.setColour (juce::Label::textColourId, renoise::text);
    headerLabel.setColour (juce::Label::backgroundColourId, renoise::panel);
    headerLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    addAndMakeVisible (headerLabel);

    addAndMakeVisible (waveform);
    addAndMakeVisible (platter);

    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    infoLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (infoLabel);

    setSize (560, 470);
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
    waveform.setBounds (r.removeFromTop (130));
    r.removeFromTop (10);

    auto bottom = r.removeFromBottom (30);
    loadButton.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (10);
    infoLabel.setBounds (bottom);
    r.removeFromBottom (10);

    const int sz = juce::jmin (r.getWidth(), r.getHeight());
    platter.setBounds (r.withSizeKeepingCentre (sz, sz));
}

void ScratchAudioProcessorEditor::timerCallback()
{
    waveform.repaint();
    platter.repaint();

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
