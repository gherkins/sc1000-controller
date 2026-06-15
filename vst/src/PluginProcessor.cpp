#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kStateMagic = 0x53435231; // 'SCR1'

    // Downmix any multi-channel buffer to a single mono channel (average of the
    // channels). Returns the buffer unchanged if it is already mono. The plugin is
    // mono end-to-end: one waveform, half the data, and the engine fans the single
    // channel out to the stereo bus on playback.
    std::shared_ptr<juce::AudioBuffer<float>> toMono (std::shared_ptr<juce::AudioBuffer<float>> buf)
    {
        if (buf == nullptr || buf->getNumChannels() <= 1)
            return buf;

        const int nc = buf->getNumChannels();
        const int ns = buf->getNumSamples();
        auto mono = std::make_shared<juce::AudioBuffer<float>> (1, ns);
        mono->clear();

        auto* d = mono->getWritePointer (0);
        for (int c = 0; c < nc; ++c)
        {
            const auto* s = buf->getReadPointer (c);
            for (int i = 0; i < ns; ++i)
                d[i] += s[i];
        }
        juce::FloatVectorOperations::multiply (d, 1.0f / (float) nc, ns);
        return mono;
    }

    // Peak-normalize in place to `target` (no-op if silent). Keeps loaded samples at
    // a consistent, hot level regardless of how quiet the source file was.
    void normalizePeak (juce::AudioBuffer<float>& buf, float target)
    {
        float peak = 0.0f;
        for (int c = 0; c < buf.getNumChannels(); ++c)
            peak = juce::jmax (peak, buf.getMagnitude (c, 0, buf.getNumSamples()));
        if (peak > 1.0e-6f)
            buf.applyGain (target / peak);
    }
}

ScratchAudioProcessor::ScratchAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    formatManager.registerBasicFormats();
}

void ScratchAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    engine.prepare (sampleRate);
}

void ScratchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // --- decode the SC1000 MIDI contract (docs/MIDI-MAPPING.md) from raw bytes ---
    for (const auto meta : midi)
    {
        if (meta.numBytes < 3)
            continue;

        const auto* b = meta.data;
        const int status = b[0] & 0xF0; // ignore channel — it's configurable on the device

        if (status == 0xB0) // Control Change
        {
            const int cc = b[1] & 0x7F;
            const int v  = b[2] & 0x7F;
            switch (cc)
            {
                case 16: controlState.crossfader.store ((float) v / 127.0f); break; // crossfader
                case 18: controlState.volA.store ((float) v / 127.0f); break;       // volume pot A
                case 19: controlState.volB.store ((float) v / 127.0f); break;       // volume pot B
                case 20:                                                            // jog: relative delta
                {
                    const int delta = (v < 64) ? v : v - 128; // two's-complement 7-bit
                    controlState.addJog (delta);
                    break;
                }
                case 21: controlState.touched.store (v >= 64); break;               // continuous jog-touch level (firmware ≥ CC build)
                default: break; // CC17 mirrors CC16 — ignored
            }
        }
        else if (status == 0x90 || status == 0x80) // Note On / Off
        {
            const int note = b[1] & 0x7F;
            const int vel  = b[2] & 0x7F;
            const bool pressed = (status == 0x90 && vel > 0); // vel-0 note-on == release
            if (note == 20)
                controlState.touched.store (pressed); // jog touch
            else if (pressed && (note == 26 || note == 27 || (note >= 32 && note <= 35)))
                controlState.togglePlaying(); // Start/Stop taps (26/27) + cue pads (32-35) → toggle play
            // Back buttons 21-24 and Shift 25 reserved — ARCHITECTURE.md open question 6.
        }
    }

    engine.process (buffer, controlState);
}

juce::AudioProcessorEditor* ScratchAudioProcessor::createEditor()
{
    return new ScratchAudioProcessorEditor (*this);
}

// ---- sample loading ----

bool ScratchAudioProcessor::loadFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int ns = (int) reader->lengthInSamples;
    const int ch = (int) reader->numChannels;
    if (ns <= 0 || ch <= 0)
        return false;

    auto buf = std::make_shared<juce::AudioBuffer<float>> (ch, ns);
    reader->read (buf.get(), 0, ns, 0, true, true);
    applyLoadedBuffer (std::move (buf), reader->sampleRate, file.getFileName());
    return true;
}

void ScratchAudioProcessor::applyLoadedBuffer (std::shared_ptr<juce::AudioBuffer<float>> buf,
                                               double srcRate, const juce::String& name)
{
    buf = toMono (std::move (buf)); // mono throughout — one waveform, half the data
    normalizePeak (*buf, 0.95f);    // consistent, hot level (source files vary in level)

    loadedBuffer = buf;
    loadedRate   = srcRate;
    sampleName   = name;

    engine.setSample (buf, srcRate);

    thumbnail.reset (buf->getNumChannels(), srcRate, buf->getNumSamples());
    thumbnail.addBlock (0, *buf, 0, buf->getNumSamples());

    sampleGeneration.fetch_add (1);
}

// ---- state: embed the sample so the .xrns stays self-contained ----

void ScratchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream os (destData, false);
    os.writeInt (kStateMagic);
    os.writeInt (2); // version 2: sample stored as FLAC (v1 was raw float PCM)

    auto buf = loadedBuffer;
    if (buf == nullptr)
    {
        os.writeInt (0); // no sample
        return;
    }

    os.writeInt (1); // has sample
    os.writeString (sampleName);
    os.writeDouble (loadedRate);

    // Encode the sample as FLAC — lossless (to 24-bit, transparent) and far smaller
    // than raw float PCM inside the .xrns. It is decoded back to a plain PCM buffer
    // once on load; the audio thread never touches FLAC, so realtime scratching is
    // identical regardless of how the sample was stored.
    juce::MemoryBlock flacBlock;
    {
        juce::FlacAudioFormat flac;
        auto mos = std::make_unique<juce::MemoryOutputStream> (flacBlock, false);
        if (auto* writer = flac.createWriterFor (mos.get(), loadedRate,
                                                 (unsigned int) buf->getNumChannels(),
                                                 24, {}, 0))
        {
            mos.release(); // writer owns the stream now
            writer->writeFromAudioSampleBuffer (*buf, 0, buf->getNumSamples());
            delete writer; // flushes + finalizes the FLAC stream into flacBlock
        }
    }

    os.writeInt64 ((juce::int64) flacBlock.getSize());
    os.write (flacBlock.getData(), flacBlock.getSize());
}

void ScratchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream is (data, (size_t) sizeInBytes, false);
    if (is.readInt() != kStateMagic)
        return;
    const int version = is.readInt();
    if (is.readInt() == 0)
        return; // no sample stored

    const juce::String name = is.readString();
    const double rate = is.readDouble();

    std::shared_ptr<juce::AudioBuffer<float>> buf;

    if (version >= 2) // FLAC blob
    {
        const juce::int64 flacSize = is.readInt64();
        if (flacSize <= 0)
            return;

        juce::MemoryBlock flacBlock;
        flacBlock.setSize ((size_t) flacSize);
        is.read (flacBlock.getData(), (int) flacSize);

        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (
            flac.createReaderFor (new juce::MemoryInputStream (flacBlock, false), true));
        if (reader == nullptr)
            return;

        const int ch = (int) reader->numChannels;
        const int ns = (int) reader->lengthInSamples;
        if (ch <= 0 || ns <= 0)
            return;

        buf = std::make_shared<juce::AudioBuffer<float>> (ch, ns);
        reader->read (buf.get(), 0, ns, 0, true, true);
    }
    else // version 1: raw float PCM (legacy)
    {
        const int ch = is.readInt();
        const int ns = is.readInt();
        if (ch <= 0 || ns <= 0)
            return;

        buf = std::make_shared<juce::AudioBuffer<float>> (ch, ns);
        for (int c = 0; c < ch; ++c)
            is.read (buf->getWritePointer (c), (int) (sizeof (float) * (size_t) ns));
    }

    applyLoadedBuffer (std::move (buf), rate, name);
}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScratchAudioProcessor();
}
