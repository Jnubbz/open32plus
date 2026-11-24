// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// Morse timing (morse.h, docs/spec/morse.md)
#include <string.h>
#include "radio/morse.h"

// ITU-R M.1677-1 characters: a leading 1 marker, then the elements MSB first (0 dot, 1 dash)
static const uint8_t letters[26] = {
	0x05, 0x18, 0x1A, 0x0C, 0x02, 0x12, 0x0E, 0x10, 0x04, 0x17, 0x0D, 0x14, 0x07,	// A-M
	0x06, 0x0F, 0x16, 0x1D, 0x0A, 0x08, 0x03, 0x09, 0x11, 0x0B, 0x19, 0x1B, 0x1C,	// N-Z
};
static const uint8_t digits[10] = { 0x3F, 0x2F, 0x27, 0x23, 0x21, 0x20, 0x30, 0x38, 0x3C, 0x3E };

uint16_t morse_dot_ms(uint8_t wpm)
{
	if (wpm < MORSE_WPM_MIN) {
		wpm = MORSE_WPM_MIN;
	} else if (wpm > MORSE_WPM_MAX) {
		wpm = MORSE_WPM_MAX;
	}
	return (uint16_t)(1200u / wpm);
}

uint8_t morse_code(char c)
{
	if (c >= 'a' && c <= 'z') {
		c = (char)(c - 'a' + 'A');
	}
	if (c >= 'A' && c <= 'Z') {
		return letters[c - 'A'];
	}
	if (c >= '0' && c <= '9') {
		return digits[c - '0'];
	}
	switch (c) {
	case '/': return 0x32;	// -..-.
	case '-': return 0x61;	// -....-
	case '=': return 0x31;	// -...-
	case '?': return 0x4C;	// ..--..
	case '.': return 0x55;	// .-.-.-
	case ',': return 0x73;	// --..--
	case '+': return 0x2A;	// .-.-.
	case '@': return 0x5A;	// .--.-.
	case '\'': return 0x5E;	// .----.
	case '(': return 0x36;	// -.--.
	case ')': return 0x6D;	// -.--.-
	case ':': return 0x78;	// ---...
	case '"': return 0x52;	// .-..-.
	case '!': return 0x6B;	// -.-.--
	default: return 0;
	}
}

// Elements left in a code (the bits below the marker)
static bool has_elements(uint8_t code)
{
	return code > 1;
}

// The next element (true: dash), and the code without it
static bool take_element(uint8_t *code)
{
	uint8_t marker = 0x80;
	while ((*code & marker) == 0) {
		marker >>= 1;
	}
	uint8_t next = marker >> 1;
	bool dash = (*code & next) != 0;
	*code = (uint8_t)((*code & (next - 1u)) | next);	// the marker moves down past the element
	return dash;
}

// From character pos on: the next one with a code (its index, len if none) and whether a space came before it.
// in: whether pos is inside '<' ... '>' (updated to the found character's). joined: the found character continues the
// prosign the previous one is in (no '>' and no space between), so the gap before it is one dot, not three
static uint8_t next_char(const morse_t *m, uint8_t pos, bool *space, bool *in, bool *joined)
{
	bool was_in = *in;
	bool closed = false;
	*space = false;
	while (pos < m->len) {
		char c = m->text[pos];
		if (c == ' ') {
			*space = true;
		} else if (c == '<') {
			*in = true;
		} else if (c == '>') {
			*in = false;
			closed = true;
		} else if (morse_code(c) != 0) {
			*joined = was_in && *in && !closed && !*space;
			return pos;
		}
		pos++;
	}
	*joined = false;
	return m->len;
}

// The gap kinds a segment can be: a plain run of units, or the spacing between characters or words (the gaps
// Farnsworth spacing stretches; their length is in morse_t, their standard units in units)
enum { SEG_UNITS, SEG_CHAR_GAP, SEG_WORD_GAP };

// The next segment: key down or up for units (kind: how long it lasts); false when the text is over
static bool next_segment(morse_t *m, bool *key, uint8_t *units, uint8_t *kind)
{
	*kind = SEG_UNITS;
	if (m->done) {
		return false;
	}
	if (m->gap_due) {
		m->gap_due = false;
		if (has_elements(m->code)) {
			*key = false;
			*units = 1;
			return true;
		}
		bool space;
		bool joined;
		uint8_t pos = next_char(m, (uint8_t)(m->pos + 1u), &space, &m->prosign, &joined);
		if (pos >= m->len) {
			m->done = true;
			return false;
		}
		m->pos = pos;
		m->code = morse_code(m->text[pos]);
		*key = false;
		if (joined) {
			*units = 1;		// the next letter of a prosign: keyed as one character
		} else {
			*units = space ? 7 : 3;
			*kind = space ? SEG_WORD_GAP : SEG_CHAR_GAP;
		}
		return true;
	}
	*key = true;
	*units = take_element(&m->code) ? 3 : 1;
	m->gap_due = true;
	return true;
}

static void load(morse_t *m, const char *text, uint8_t len)
{
	memset(m, 0, sizeof(*m));
	if (len > MORSE_TEXT_MAX) {
		len = MORSE_TEXT_MAX;
	}
	memcpy(m->text, text, len);
	m->len = len;
	bool space;
	bool joined;
	m->pos = next_char(m, 0, &space, &m->prosign, &joined);
	if (m->pos >= m->len) {
		m->done = true;
		return;
	}
	m->code = morse_code(m->text[m->pos]);
}

uint32_t morse_units(const char *text, uint8_t len)
{
	morse_t m;
	bool key;
	uint8_t units;
	uint8_t kind;
	uint32_t total = 0;
	load(&m, text, len);
	while (next_segment(&m, &key, &units, &kind)) {
		total += units;
	}
	return total;
}

static uint8_t clamp_wpm(uint8_t wpm)
{
	return wpm < MORSE_WPM_MIN ? MORSE_WPM_MIN : (wpm > MORSE_WPM_MAX ? MORSE_WPM_MAX : wpm);
}

void morse_gap_ms(uint8_t wpm, uint8_t eff_wpm, uint16_t *char_ms, uint16_t *word_ms)
{
	uint32_t c = clamp_wpm(wpm);
	uint32_t dot = morse_dot_ms(wpm);
	if (eff_wpm == 0 || clamp_wpm(eff_wpm) >= c) {
		*char_ms = (uint16_t)(3u * dot);
		*word_ms = (uint16_t)(7u * dot);
		return;
	}
	uint32_t s = clamp_wpm(eff_wpm);
	// ARRL (QST, April 1990, KE3Z): the spacing a 50 unit word needs at s overall, t_a = (60c - 37.2s) / (sc) seconds,
	// shared over the word's 19 units of character and word gaps: 3 t_a / 19 and 7 t_a / 19. In ms, times 19:
	// 19 * t_a = (600c - 372s) * 100 / (sc), rounded once at the end
	uint32_t num = (600u * c - 372u * s) * 100u;
	uint32_t den = 19u * s * c;
	*char_ms = (uint16_t)((3u * num + den / 2u) / den);
	*word_ms = (uint16_t)((7u * num + den / 2u) / den);
}

uint32_t morse_ms(const char *text, uint8_t len, uint8_t wpm, uint8_t eff_wpm)
{
	morse_t m;
	bool key;
	uint8_t units;
	uint8_t kind;
	uint16_t char_ms, word_ms;
	uint32_t dot = morse_dot_ms(wpm);
	uint32_t total = 0;
	morse_gap_ms(wpm, eff_wpm, &char_ms, &word_ms);
	load(&m, text, len);
	while (next_segment(&m, &key, &units, &kind)) {
		total += kind == SEG_CHAR_GAP ? char_ms : kind == SEG_WORD_GAP ? word_ms : units * dot;
	}
	return total;
}

void morse_start_farnsworth(morse_t *m, const char *text, uint8_t len, uint8_t wpm, uint8_t eff_wpm, uint32_t now_ms)
{
	load(m, text, len);
	m->dot_ms = morse_dot_ms(wpm);
	morse_gap_ms(wpm, eff_wpm, &m->char_gap_ms, &m->word_gap_ms);
	m->until = now_ms;
}

void morse_start(morse_t *m, const char *text, uint8_t len, uint8_t wpm, uint32_t now_ms)
{
	morse_start_farnsworth(m, text, len, wpm, 0, now_ms);
}

bool morse_poll(morse_t *m, uint32_t now_ms)
{
	while (!m->done && (int32_t)(now_ms - m->until) >= 0) {
		bool key;
		uint8_t units;
		uint8_t kind;
		if (!next_segment(m, &key, &units, &kind)) {
			break;
		}
		m->key = key;
		m->units += units;
		m->until += kind == SEG_CHAR_GAP ? m->char_gap_ms :
			    kind == SEG_WORD_GAP ? m->word_gap_ms : (uint32_t)units * m->dot_ms;
	}
	if (m->done) {
		m->key = false;
	}
	return m->key;
}
