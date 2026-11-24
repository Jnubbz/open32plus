// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// Morse from received FM audio (MCW, docs/spec/morse.md 7): the tone found between 400 and 1000 Hz, keyed on and off
// against levels that follow the signal, the speed learned from the elements, text out as characters arrive.
// 8 kHz signed 16-bit samples in 10 ms blocks; no clock of its own, so call morse_dec_flush when the audio stops.
#ifndef MORSE_DECODE_H
#define MORSE_DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MDEC_RATE	8000u
#define MDEC_BLOCK	80u	// samples: 10 ms, bins 100 Hz wide
#define MDEC_BLOCK_MS	10u
#define MDEC_BINS	13u	// 400 to 1000 Hz in 50 Hz steps
#define MDEC_HZ_MIN	400u
#define MDEC_HZ_STEP	50u
#define MDEC_OUT_MAX	8u	// the most text one block can end ("<SK>", a space, one more character)
#define MDEC_HIST	8u	// marks and gaps remembered to learn the speed and spacing from
#define MDEC_EL_MAX	8u	// the most elements in a character (seven in the longest, $)

typedef struct {
	int32_t s1[MDEC_BINS], s2[MDEC_BINS];	// Goertzel state of the block being summed
	uint16_t n;				// samples in it
	uint8_t bin;				// the tone locked on
	uint8_t cand, cand_n;			// a stronger bin seen in a row, toward a new lock
	bool locked;
	uint32_t floor, peak;			// the locked bin's level with the key up and down, following the signal
	bool on;				// the key as decoded
	uint16_t run;				// blocks in the current state
	uint16_t dot_ms, dash_ms;		// the element lengths learned (dot = 1200 / wpm)
	uint16_t gap_ms, word_ms;		// the gaps between characters and between words learned
	uint16_t marks[MDEC_HIST], gaps[MDEC_HIST];	// the last marks, and gaps longer than an element gap
	uint8_t mark_n, gap_n;			// how many of each so far (to MDEC_HIST; the next goes at n % MDEC_HIST)
	uint16_t el[MDEC_EL_MAX];		// the marks of the character being heard, classed when it ends
	uint8_t el_n;				// how many (more than MDEC_EL_MAX: '*')
	bool char_out, word_out;		// this gap has ended a character / a word already
	uint8_t word_chars;			// characters since the last word gap
	uint32_t chars;				// characters decoded (statistics)
} morse_dec_t;

void morse_dec_init(morse_dec_t *d);
// Feed samples; the text decoded meanwhile is appended to out (at most max bytes, no terminator). Returns its length
size_t morse_dec_feed(morse_dec_t *d, const int16_t *s, size_t n, char *out, size_t max);
// The audio stopped (squelch closed): a character in progress ends, then a word space, and the speed and spacing are
// learned afresh from the next over (the ones so far as the first guess). Returns the text's length
size_t morse_dec_flush(morse_dec_t *d, char *out, size_t max);
// The tone locked on in Hz, 0 for none yet
uint16_t morse_dec_hz(const morse_dec_t *d);
// The speed learned in wpm
uint8_t morse_dec_wpm(const morse_dec_t *d);
// The text for a code in morse.c's form (marker, then elements MSB first, 0 dot 1 dash) into out (4 bytes, no
// terminator): one character, a prosign without one of its own in brackets ("<SK>"), or '*' when unknown. Its length
uint8_t morse_char(uint8_t code, char *out);

#endif
