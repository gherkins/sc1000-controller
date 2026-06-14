// Mixxx controller script for the SC1000 (Stage 1.5)
//
// Pairs with sc1000.midi.xml. Turns the SC1000's MIDI output into a scratchable
// Mixxx deck:
//   - Jog rotation (CC 20, relative two's-complement) -> scratch / pitch bend
//   - Jog touch    (Note 20 on/off)                   -> scratch enable / disable
// Crossfader/pot CCs are mapped directly in the XML (see notes there).
//
// Load via Preferences > Controllers. Tune the constants below to taste.

var SC1000 = {};

// --- tunables ---------------------------------------------------------------
SC1000.deck = 1;                  // which Mixxx deck the jog scratches
SC1000.intervalsPerRev = 4096;    // counts the firmware emits per platter rev (AS5601 = 4096)
SC1000.rpm = 33 + 1 / 3;          // virtual platter speed
SC1000.alpha = 1.0 / 8;           // scratch filter (Mixxx defaults)
SC1000.beta = (1.0 / 8) / 32;

SC1000.init = function (id, debug) {};
SC1000.shutdown = function () {};

// relative two's-complement 7-bit -> signed int
SC1000.rel = function (value) {
    return (value < 0x40) ? value : value - 0x80;
};

// Jog touch: Note 20 on (0x9x) = touched -> scratch; off (0x8x or vel 0) = release
SC1000.jogTouch = function (channel, control, value, status, group) {
    var deck = SC1000.deck;
    if ((status & 0xF0) === 0x90 && value > 0) {
        engine.scratchEnable(deck, SC1000.intervalsPerRev, SC1000.rpm,
                             SC1000.alpha, SC1000.beta);
    } else {
        engine.scratchDisable(deck);
    }
};

// Jog rotation: CC 20 relative. Scratch while touched, else nudge pitch.
SC1000.jogTick = function (channel, control, value, status, group) {
    var deck = SC1000.deck;
    var interval = SC1000.rel(value);
    if (engine.isScratching(deck)) {
        engine.scratchTick(deck, interval);
    } else {
        engine.setValue("[Channel" + deck + "]", "jog", interval);
    }
};
