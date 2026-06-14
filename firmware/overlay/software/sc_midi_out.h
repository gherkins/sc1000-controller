/*
 * SC1000 USB-MIDI controller output
 *
 * Emits the crossfader/volume ADCs, the jog-wheel rotation, and the jog touch
 * state as MIDI to a USB-MIDI gadget, so the SC1000 can act as a controller for
 * a host VST / Mixxx. See controller-mod/README.md for the wiring and MIDI map.
 *
 * This is an SC1000-specific addition (not part of upstream xwax).
 */

#ifndef SC_MIDI_OUT_H
#define SC_MIDI_OUT_H

#include <stdbool.h>

/*
 * Open the USB-MIDI gadget for output. Uses scsettings.midioutdev if set,
 * otherwise auto-detects a rawmidi device whose name/description looks like a
 * USB MIDI gadget. Safe to call when no gadget is present (output just stays
 * disabled and is retried lazily from sc_midi_out_update).
 *
 * Return: 0 if an output device was opened, -1 otherwise.
 */
int sc_midi_out_init(void);

/*
 * Called from the input thread once per loop. Sends crossfader/volume as CC,
 * jog rotation as relative CC, and jog touch as note on/off - throttled to
 * scsettings.midioutrate microseconds and only when values change.
 *
 *   encoderAngle : 0..4095 absolute platter angle (already de-blipped/reversed)
 *   adc0..adc3   : 10-bit fader / pot ADC values (0..1023)
 *   touched      : capacitive jog-touch state
 */
void sc_midi_out_update(int encoderAngle, unsigned int adc0, unsigned int adc1,
                        unsigned int adc2, unsigned int adc3, bool touched);

#endif
