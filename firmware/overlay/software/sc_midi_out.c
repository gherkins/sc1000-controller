/*
 * SC1000 USB-MIDI controller output
 *
 * Sends the crossfader/volume ADCs as CC, the jog-wheel rotation as a relative
 * CC, and the jog touch as note on/off, to a USB-MIDI gadget. This lets the
 * SC1000 drive a host VST / Mixxx (Route 1). See controller-mod/README.md.
 *
 * MIDI map (channel = scsettings.midioutchannel, default 0):
 *   CC 16  crossfader input 1   (0..127)
 *   CC 17  crossfader input 2   (0..127)
 *   CC 18  volume pot 1         (0..127)
 *   CC 19  volume pot 2         (0..127)
 *   CC 20    jog, relative two's-complement 7-bit (1..63 fwd, 127..65 rev)
 *   CC 21    jog touch level, continuous 0/127 - re-sent every flush, robust to dropped edges
 *   CC 22    capsense level (PIC smoothed touchAverage >> 3, 0..127; lower =
 *            more touch) - the analog signal behind the CC21/Note20 verdict,
 *            for host-side touch detection / margin tracing
 *   Note 20  jog touch edge (note-on touched / note-off released) - kept for edge-based hosts
 *   Note 21-24  buttons (PIC: sample/beat prev/next) - map any to start/stop etc.
 *   Note 25  Shift (front)        on while held, off on release
 *   Note 26/27  Start/Stop (front) momentary tap (deck 0 / deck 1)
 *   Note 32+  cue buttons (top)    momentary tap, NOTE_CUE_BASE + expander pin
 *
 * This is an SC1000-specific addition (not part of upstream xwax).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "midi.h"
#include "xwax.h"
#include "sc_midimap.h"
#include "sc_midi_out.h"

/* MIDI assignments (status nibble | channel applied at send time) */
#define CC_XFADER1    16
#define CC_XFADER2    17
#define CC_VOLUME1    18
#define CC_VOLUME2    19
#define CC_JOG        20
#define CC_JOGTOUCH   21 /* continuous jog-touch level (0/127), re-sent every flush */
#define CC_CAPLEVEL   22 /* capsense analog level (10-bit >> 3), sent on change */
#define NOTE_JOGTOUCH 20
#define NOTE_BUTTON0  21 /* PIC buttons 0..3 -> notes 21..24 */

/* IO buttons (read via process_io / IOevent, not the PIC) */
#define NOTE_SHIFT       25 /* front Shift: on while held, off on release */
#define NOTE_STARTSTOP   26 /* front Start/Stop: +DeckNo -> 26 (CH0) / 27 (CH1) */
#define NOTE_CUE_BASE    32 /* top cue buttons: NOTE_CUE_BASE + expander pin */

/* the 4 PIC buttons (back panel: sample/beat prev/next), read in sc_input.c */
extern unsigned char buttons[4];

static struct midi out;
static bool ready = false;
static bool disabled = false;

static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* case-insensitive substring test (avoids relying on GNU strcasestr) */
static int ci_contains(const char *hay, const char *needle)
{
	if (!hay || !needle)
		return 0;
	size_t nl = strlen(needle);
	if (nl == 0)
		return 1;
	for (const char *p = hay; *p; p++)
	{
		size_t i = 0;
		while (i < nl && p[i] &&
			   tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
			i++;
		if (i == nl)
			return 1;
	}
	return 0;
}

/*
 * Find a rawmidi device that looks like our USB-MIDI gadget and open it.
 * Returns 0 on success.
 */
static int open_gadget(void)
{
	/* explicit override from scsettings.txt */
	if (scsettings.midioutdev[0] != '\0')
	{
		printf("sc_midi_out: opening configured device '%s'\n", scsettings.midioutdev);
		return midi_open(&out, scsettings.midioutdev);
	}

	void **hints, **n;
	char chosen[80];
	chosen[0] = '\0';

	if (snd_device_name_hint(-1, "rawmidi", &hints) != 0)
		return -1;

	for (n = hints; *n != NULL; n++)
	{
		char *name = snd_device_name_get_hint(*n, "NAME");
		char *desc = snd_device_name_get_hint(*n, "DESC");

		if (chosen[0] == '\0' &&
			(ci_contains(desc, "gadget") || ci_contains(desc, "f_midi") ||
			 ci_contains(name, "gadget") || ci_contains(name, "f_midi")))
		{
			if (name)
			{
				strncpy(chosen, name, sizeof(chosen) - 1);
				chosen[sizeof(chosen) - 1] = '\0';
			}
		}

		if (name)
			free(name);
		if (desc)
			free(desc);
	}
	snd_device_name_free_hint(hints);

	if (chosen[0] == '\0')
	{
		printf("sc_midi_out: no USB-MIDI gadget rawmidi found yet\n");
		return -1;
	}

	printf("sc_midi_out: opening gadget '%s'\n", chosen);
	return midi_open(&out, chosen);
}

int sc_midi_out_init(void)
{
	if (!scsettings.midiout)
	{
		disabled = true;
		printf("sc_midi_out: disabled in settings\n");
		return -1;
	}
	if (open_gadget() == 0)
	{
		ready = true;
		printf("sc_midi_out: ready\n");
		return 0;
	}
	return -1;
}

static void send3(unsigned char s, unsigned char d1, unsigned char d2)
{
	unsigned char buf[3] = {s, d1, d2};
	midi_write(&out, buf, 3);
}

/* Send a CC only when it moves at least `deadband` LSB (or hits an extreme),
   to suppress the +/-1 LSB ADC jitter that otherwise floods the bus. */
static void send_cc(int cc, int val7, int *prev, int deadband)
{
	if (val7 == *prev)
		return;
	if (val7 == 0 || val7 == 127 || abs(val7 - *prev) >= deadband)
	{
		send3(0xB0 | (scsettings.midioutchannel & 0x0F),
			  (unsigned char)cc, (unsigned char)val7);
		*prev = val7;
	}
}

/*
 * Hook from IOevent(): a physical IO button has just been actioned. We expose
 * the front/top buttons the PIC doesn't carry - the 4 cue buttons, Shift and
 * Start/Stop - as MIDI. Other actions (file/folder nav, volume, etc.) and
 * MIDI-driven maps are ignored so we never echo host MIDI back out.
 *
 * Cue and Start/Stop only get a press edge from the firmware (no release), so
 * they go out as a momentary note tap (on then off). Shift toggles a held note
 * (on at SHIFTON, off at SHIFTOFF) so the host can read the shift state.
 */
void sc_midi_out_io_event(const struct mapping *map)
{
	if (disabled || !ready || map == NULL || map->Type != MAP_IO)
		return;

	unsigned char ch = scsettings.midioutchannel & 0x0F;

	switch (map->Action)
	{
	case ACTION_CUE:
	case ACTION_DELETECUE: /* same physical pad, shifted; host reads Shift note */
		send3(0x90 | ch, NOTE_CUE_BASE + map->Pin, 127);
		send3(0x80 | ch, NOTE_CUE_BASE + map->Pin, 0);
		break;
	case ACTION_STARTSTOP:
		send3(0x90 | ch, NOTE_STARTSTOP + (map->DeckNo & 0x01), 127);
		send3(0x80 | ch, NOTE_STARTSTOP + (map->DeckNo & 0x01), 0);
		break;
	case ACTION_SHIFTON:
		send3(0x90 | ch, NOTE_SHIFT, 127);
		break;
	case ACTION_SHIFTOFF:
		send3(0x80 | ch, NOTE_SHIFT, 0);
		break;
	default:
		break;
	}
}

void sc_midi_out_update(int encoderAngle, unsigned int adc0, unsigned int adc1,
						unsigned int adc2, unsigned int adc3, bool touched,
						unsigned int capLevel)
{
	static uint64_t lastFlush = 0;
	static uint64_t lastTry = 0;
	static int prevX1 = -1, prevX2 = -1, prevV1 = -1, prevV2 = -1;
	static int prevTouched = -1;
	static int prevAngle = -1;
	uint64_t t;

	if (disabled)
		return;

	if (!ready)
	{
		/* the gadget may come up shortly after boot - retry a couple of times */
		t = now_us();
		if (t - lastTry > 2000000ull)
		{
			lastTry = t;
			sc_midi_out_init();
			if (ready)
				prevAngle = encoderAngle; /* seed so first delta isn't huge */
		}
		return;
	}

	t = now_us();
	if (t - lastFlush < (uint64_t)scsettings.midioutrate)
		return;
	lastFlush = t;

	unsigned char ch = scsettings.midioutchannel & 0x0F;

	/* Faders / pots: 10-bit -> 7-bit, with a deadband against +/-1 LSB jitter */
	send_cc(CC_XFADER1, adc0 >> 3, &prevX1, 2);
	send_cc(CC_XFADER2, adc1 >> 3, &prevX2, 2);
	send_cc(CC_VOLUME1, adc2 >> 3, &prevV1, 2);
	send_cc(CC_VOLUME2, adc3 >> 3, &prevV2, 2);

	/* Jog touch: continuous level CC, re-sent every flush so a dropped or spurious
	   packet self-heals on the next cycle - the host reads the live level, not edges.
	   Keep the Note 20 on/off edge too, for edge-based hosts (e.g. Mixxx). */
	send3(0xB0 | ch, CC_JOGTOUCH, touched ? 127 : 0);
	int tnow = touched ? 1 : 0;
	if (tnow != prevTouched)
	{
		if (tnow)
			send3(0x90 | ch, NOTE_JOGTOUCH, 127);
		else
			send3(0x80 | ch, NOTE_JOGTOUCH, 0);
		prevTouched = tnow;
	}

	/* Capsense analog level behind that verdict (lower = more touch). Sent on
	   change only - the PIC's EMA moves slowly, so this stays quiet at rest. */
	static int prevCapLevel = -1;
	send_cc(CC_CAPLEVEL, (int)(capLevel >> 3), &prevCapLevel, 1);

	/* Buttons (PIC: back sample/beat prev/next) -> notes 21..24.
	   Map any to start/stop, cue, load, etc. in the host. */
	static int prevButtons = 0;
	int curButtons = (buttons[0] ? 1 : 0) | (buttons[1] ? 2 : 0) |
					 (buttons[2] ? 4 : 0) | (buttons[3] ? 8 : 0);
	for (int bi = 0; bi < 4; bi++)
	{
		if (((curButtons >> bi) & 1) != ((prevButtons >> bi) & 1))
		{
			if ((curButtons >> bi) & 1)
				send3(0x90 | ch, NOTE_BUTTON0 + bi, 127);
			else
				send3(0x80 | ch, NOTE_BUTTON0 + bi, 0);
		}
	}
	prevButtons = curButtons;

	/* Jog movement: accumulated delta since last flush, handling platter wrap */
	if (prevAngle < 0)
		prevAngle = encoderAngle;
	int delta = encoderAngle - prevAngle;
	if (delta > 2048)
		delta -= 4096;
	else if (delta < -2048)
		delta += 4096;
	prevAngle = encoderAngle;

	while (delta != 0)
	{
		int step = delta;
		if (step > 63)
			step = 63;
		else if (step < -63)
			step = -63;
		/* two's-complement 7-bit relative value */
		send3(0xB0 | ch, CC_JOG, (unsigned char)(step & 0x7F));
		delta -= step;
	}
}
