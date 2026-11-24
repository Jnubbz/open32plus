// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// Morse timing (docs/spec/morse.md), used by the CW ID (the hotspot's SEND_CWID, docs/spec/mmdvm-protocol.md 2.11):
// text to key-down / key-up segments in units of one dot, at a speed in words per minute on the PARIS standard (a word
// is 50 units, so a dot is 1200 / wpm ms). Dot 1 unit, dash 3, the gap inside a character 1, between characters 3,
// between words 7 (ITU-R M.1677-1). Optional Farnsworth spacing stretches only the gaps between characters and words.
// Letters between '<' and '>' are keyed as one prosign (<AR>, <SK>). No hardware here: the radio core turns the key
// into the board's tone (hal_radio_t tx_tone / tx_tone_key).
#ifndef RADIO_MORSE_H
#define RADIO_MORSE_H

#include <stdint.h>
#include <stdbool.h>

#define MORSE_WPM_DEFAULT	20
#define MORSE_WPM_MIN		5
#define MORSE_WPM_MAX		40
#define MORSE_TONE_HZ_DEFAULT	800
#ifndef MORSE_TEXT_MAX
#define MORSE_TEXT_MAX		40	// characters keyed; a longer ID is cut (the SDR bench sends up to 40, MMDVMHost a callsign)
#endif
#if MORSE_TEXT_MAX > 255
#error "MORSE_TEXT_MAX must fit morse_t's uint8_t positions"
#endif

typedef struct {
	char text[MORSE_TEXT_MAX];
	uint8_t len;
	uint8_t pos;		// the character being keyed
	uint8_t code;		// its elements still to send, with the leading marker bit (morse_code); 1 = none left
	bool gap_due;		// the gap after the element just keyed
	uint16_t dot_ms;
	uint16_t char_gap_ms;	// the gap between characters: 3 dots, longer with Farnsworth spacing
	uint16_t word_gap_ms;	// the gap between words: 7 dots, longer with Farnsworth spacing
	bool prosign;		// the character being keyed is inside '<' ... '>'
	bool key;		// key down now
	bool done;
	uint32_t until;		// end of the current segment (ms)
	uint32_t units;		// units keyed so far (key down and up), for the tests
} morse_t;

// The dot length for a speed (clamped to MORSE_WPM_MIN..MAX)
uint16_t morse_dot_ms(uint8_t wpm);
// A character's elements, MSB first after a leading 1 marker: 0 dot, 1 dash (A .- = 0b101). 0 for a character with no
// Morse (skipped), letters in either case
uint8_t morse_code(char c);
// The units a text takes at standard spacing, from the first element to the end of the last (no trailing gap)
uint32_t morse_units(const char *text, uint8_t len);

// The gaps between characters and between words for a character speed wpm and an overall (Farnsworth) speed eff_wpm,
// both clamped to MORSE_WPM_MIN..MAX. eff_wpm 0 or not below wpm: the standard 3 and 7 dots. Otherwise the ARRL
// Farnsworth timing: the spacing that brings a 50 unit word down to eff_wpm, shared 3:7 between the two gaps
void morse_gap_ms(uint8_t wpm, uint8_t eff_wpm, uint16_t *char_ms, uint16_t *word_ms);

// Start keying text at now_ms (the first element starts at once)
void morse_start(morse_t *m, const char *text, uint8_t len, uint8_t wpm, uint32_t now_ms);
// The same with Farnsworth spacing: characters at wpm, the text as a whole at eff_wpm (morse_gap_ms)
void morse_start_farnsworth(morse_t *m, const char *text, uint8_t len, uint8_t wpm, uint8_t eff_wpm, uint32_t now_ms);
// Run with the time (every few ms): true while the key is down. m->done once the last element has ended
bool morse_poll(morse_t *m, uint32_t now_ms);

#endif
