// Headless GUI snapshot: renders the plugin editor to /tmp/scratch_gui.png without
// a screen (offscreen CoreGraphics). A dev aid for iterating on the look without
// needing Screen Recording permission or a running DAW.
//
//   ./build.sh && ./build/ScratchShot_artefacts/Debug/ScratchShot [out.png]

#include "../src/PluginProcessor.h"
#include "../src/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    ScratchAudioProcessor proc;
    proc.prepareToPlay (44100.0, 512);

    // Synthesise a short, transient-rich sample and write a temp WAV to load,
    // so the waveform panel shows a real trace.
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("scratchshot_tone.wav");
    {
        const int sr = 44100, len = (int) (1.6 * sr);
        juce::AudioBuffer<float> buf (2, len);
        for (int i = 0; i < len; ++i)
        {
            const float t   = (float) i / (float) sr;
            const float env = 0.55f * (0.5f + 0.5f * std::sin (t * 7.0f));
            float s = env * std::sin (juce::MathConstants<float>::twoPi * 220.0f * t)
                    + 0.30f * env * std::sin (juce::MathConstants<float>::twoPi * 660.0f * t);
            if ((i % (sr / 4)) < 250) s += 0.5f * ((float) (i % 11) / 11.0f - 0.5f); // ticks
            buf.setSample (0, i, s);
            buf.setSample (1, i, s * 0.9f);
        }
        juce::WavAudioFormat wav;
        tmp.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (tmp.createOutputStream()))
            if (auto* w = wav.createWriterFor (os.get(), sr, 2, 16, {}, 0))
            {
                os.release();
                w->writeFromAudioSampleBuffer (buf, 0, len);
                delete w;
            }
    }
    proc.loadFile (tmp);

    if (argc > 2 && juce::String (argv[2]) == "play") // render the PLAY (motor-on) state
        proc.getControlState().togglePlaying();

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (ed->getWidth(), ed->getHeight());

    juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
    {
        juce::Graphics g (img);
        ed->paintEntireComponent (g, false);
    }

    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::String ("/tmp/scratch_gui.png"));
    out.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (out.createOutputStream()))
        png.writeImageToStream (img, *os);

    juce::Logger::writeToLog ("wrote " + out.getFullPathName());
    return 0;
}
