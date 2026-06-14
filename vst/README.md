# vst (Stage 2)

The custom instrument: **drop in a sample, manipulate it with the SC1000.**

Planned as a [JUCE](https://juce.com) plugin (VST3/AU):
- load/drop a sample into a buffer
- **jog (CC 20, relative)** drives the playhead velocity → variable-rate,
  reversible scrubbing (the scratch)
- **jog touch (Note 20)** toggles scratch vs. inertia/slip
- **crossfader (CC 16/17)** gates / sets gain (the on-off/volume cut)
- **volume pots (CC 18/19)** set level
- **waveform + playhead display** — directly solves the SC1000's biggest UX gripe
  (a static jog gives no sense of where you are in the sample)

Not started yet — Stage 1 (firmware MIDI out) and Stage 1.5 (Mixxx feel-test)
come first.
