#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#include "ControlState.h"
#include "ScratchEngine.h"

// AU / Standalone instrument: receives the SC1000's raw MIDI (jog/crossfader/
// touch), scratches a dropped sample, and embeds that sample in its own state so
// the song stays self-contained (ARCHITECTURE.md "Self-contained saving").
class ScratchAudioProcessor : public juce::AudioProcessor
{
public:
    ScratchAudioProcessor();
    ~ScratchAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Scratch VST"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- used by the editor ----
    bool loadFile (const juce::File&);
    juce::AudioThumbnail& getThumbnail() noexcept { return thumbnail; }
    int    getSampleGeneration() const noexcept { return sampleGeneration.load(); }
    double getPlayheadSeconds()  const noexcept { return engine.getPlayheadSeconds(); }
    double getLengthSeconds()    const noexcept { return engine.getLengthSeconds(); }
    bool   hasSample()           const noexcept { return engine.hasSample(); }
    juce::String getSampleName() const { return sampleName; }
    ControlState& getControlState() noexcept { return controlState; }

private:
    void applyLoadedBuffer (std::shared_ptr<juce::AudioBuffer<float>>, double srcRate, const juce::String& name);

    ControlState  controlState;
    ScratchEngine engine;

    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail      thumbnail { 512, formatManager, thumbnailCache };

    // Kept so the sample can be re-encoded into the saved state (self-contained .xrns).
    std::shared_ptr<juce::AudioBuffer<float>> loadedBuffer;
    double       loadedRate = 0.0;
    juce::String sampleName;
    std::atomic<int> sampleGeneration { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScratchAudioProcessor)
};
