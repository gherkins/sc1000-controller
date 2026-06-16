#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int kStateMagic = 0x53435231; // 'SCR1'

    // --- Shift-layer crossfader mappings (fader position 0..1 → a parameter) ---
    // These shape how the *input* fader maps to each mode (snap zones, detents,
    // ranges). The engine owns how it then *applies* the resulting scalar. Tune the
    // feel of each mode here.
    constexpr float kPitchRange = 0.20f;  // ±20% varispeed at the fader extremes
    constexpr float kPitchSnap  = 0.20f;  // centre ±this (in fader units) snaps to unity
    constexpr float kVolDetent  = 0.75f;  // fader position that snaps to unity volume
    constexpr float kVolSnap    = 0.04f;  // half-width of the unity-volume detent
    constexpr float kBrakeMin   = 0.25f;  // shortest tape stop (fader left); centre = 1.0× (stock)
    constexpr float kBrakeMax   = 4.0f;   // longest tape stop (fader right)

    // Cue note → crossfader mode (0=pitch 1=volume 2=curve 3=brake). The MK2's
    // expander pins do NOT map the corners to notes 32-35 in order — verified live
    // on hardware (host/midimon): note 32 = bottom-left (cue 1), 33 = top-left
    // (cue 3), 34 = top-right (cue 4), 35 = bottom-right (cue 2). So:
    constexpr int kCueNoteToMode[4] = { 0, 2, 3, 1 }; // indexed by (note - 32)

    // Cue 1 → pitch: ±kPitchRange varispeed, snapping to unity across the centre.
    float mapPitch (float x)
    {
        const float d = x - 0.5f;
        if (std::abs (d) <= kPitchSnap)
            return 1.0f;                                    // centre detent → unity
        if (d < 0.0f)                                       // left half → slow down
            return (1.0f - kPitchRange) + (x / (0.5f - kPitchSnap)) * kPitchRange;
        const float t = (x - (0.5f + kPitchSnap)) / (0.5f - kPitchSnap); // right half → speed up
        return 1.0f + t * kPitchRange;
    }

    // Cue 2 → volume: 0→unity ramp up to a detent at kVolDetent, unity at/above it.
    float mapVolume (float x)
    {
        if (x <= kVolDetent - kVolSnap)
            return x / (kVolDetent - kVolSnap);             // silence → unity
        return 1.0f;                                        // detent + above hold unity
    }

    // Cue 4 → brake: exponential so the stock value (1.0×) sits at the fader centre.
    float mapBrake (float x)
    {
        return kBrakeMin * std::pow (kBrakeMax / kBrakeMin, x);
    }

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
                case 16: // crossfader — normally the CUT, but a shift-layer param while Shift is held
                {
                    const float x = (float) v / 127.0f;
                    controlState.faderRaw.store (x); // live physical position (for the GUI head)
                    if (controlState.shiftHeld.load())
                    {
                        switch (controlState.faderMode.load())
                        {
                            case 0: controlState.pitchScale.store (mapPitch (x));  break; // cue 1: pitch
                            case 1: controlState.volA.store       (mapVolume (x)); break; // cue 2: volume (reuses volA)
                            case 2: controlState.faderCurve.store (x);             break; // cue 3: curve (left sharp → right soft)
                            case 3: controlState.brakeScale.store (mapBrake (x));  break; // cue 4: brake / tape-stop
                            default: break;
                        }
                        // leave controlState.crossfader frozen → the cut holds steady while dialing
                    }
                    else
                    {
                        controlState.crossfader.store (x); // normal hysteresis CUT
                    }
                    break;
                }
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
            else if (note == 25)
                controlState.shiftHeld.store (pressed); // Shift: held gate for the crossfader layer
            else if (pressed && (note == 26 || note == 27))
                controlState.togglePlaying(); // Start/Stop taps (26/27) → toggle play
            else if (pressed && note >= 32 && note <= 35)
                controlState.faderMode.store (kCueNoteToMode[note - 32]); // cue pad → mode (hardware-verified)
            // Back buttons 21-24 reserved — ARCHITECTURE.md open question 6.
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
    os.writeInt (3); // version 3: + shift-layer params; v2 = FLAC sample, v1 = raw float PCM

    // Shift-layer params (v3): the pitch/curve/brake dialed in via Shift+crossfader,
    // so a reopened song restores the same feel. volA is a live pot the hardware
    // re-sends, so it is deliberately not persisted.
    os.writeInt   (controlState.faderMode.load());
    os.writeFloat (controlState.pitchScale.load());
    os.writeFloat (controlState.faderCurve.load());
    os.writeFloat (controlState.brakeScale.load());

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

    if (version >= 3) // shift-layer params precede the sample block
    {
        controlState.faderMode.store  (is.readInt());
        controlState.pitchScale.store (is.readFloat());
        controlState.faderCurve.store (is.readFloat());
        controlState.brakeScale.store (is.readFloat());
    }

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
