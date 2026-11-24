// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Josh Noble (KN0BLE)
// Host test of the Morse timing (core/radio/morse.c, docs/spec/morse.md): the added punctuation, prosigns, Farnsworth
// spacing and a longer text buffer (built with MORSE_TEXT_MAX raised, as a message sender would). The CW ID's PARIS
// timing and keying basics are in mmdvmTest.c. Expected values are worked out by hand from ITU-R M.1677-1 and the ARRL
// Farnsworth formula, not from the code.
#include <stdio.h>
#include <string.h>
#include "radio/morse.h"
#include "radio/radio_cwmsg.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// A code as dots and dashes, read independently of the encoder: skip to the marker bit, then MSB first
static void pattern(uint8_t code, char *out)
{
	int bit = 7;
	while (bit >= 0 && (code & (1u << bit)) == 0) {
		bit--;
	}
	int n = 0;
	for (bit--; bit >= 0; bit--) {
		out[n++] = (code & (1u << bit)) ? '-' : '.';
	}
	out[n] = '\0';
}

static bool is(char c, const char *expect)
{
	char p[9];
	pattern(morse_code(c), p);
	return strcmp(p, expect) == 0;
}

// Key text and record the key edges (ms from the start), polling every ms; returns the number of edges
static int edges(const char *text, uint8_t wpm, uint8_t eff, uint32_t *out, int max)
{
	morse_t m;
	morse_start_farnsworth(&m, text, (uint8_t)strlen(text), wpm, eff, 0);
	int n = 0;
	bool key = false;
	for (uint32_t t = 0; t < 120000 && !m.done; t++) {
		bool k = morse_poll(&m, t);
		if (k != key && n < max) {
			out[n++] = t;
		}
		key = k;
	}
	return n;
}

static void test_punctuation(void)
{
	// The CW ID's set, unchanged
	CHECK(is('/', "-..-.") && is('-', "-....-") && is('=', "-...-") && is('?', "..--..") && is('.', ".-.-.-") &&
	      is(',', "--..--"));
	// Added
	CHECK(is('+', ".-.-.") && is('@', ".--.-.") && is('\'', ".----.") && is('(', "-.--.") && is(')', "-.--.-") &&
	      is(':', "---...") && is('"', ".-..-.") && is('!', "-.-.--"));
	// The prosign brackets are not characters
	CHECK(morse_code('<') == 0 && morse_code('>') == 0);
}

static void test_prosigns(void)
{
	// <AR> is .-.-. keyed as one character: the same as '+'. Units: marks 1+3+1+3+1, gaps 4 = 13
	CHECK(morse_units("<AR>", 4) == 13 && morse_units("+", 1) == 13);
	// <SK> ...-.- : marks 1+1+1+3+1+3, gaps 5 = 15; plain "SK" adds a 3 unit character gap in place of 1
	CHECK(morse_units("<SK>", 4) == 15 && morse_units("SK", 2) == 17);
	// In a message: the same as with '+' in its place
	CHECK(morse_units("K <AR> K", 8) == morse_units("K + K", 5));
	// <BT> = '=', <KN> = '('
	CHECK(morse_units("<BT>", 4) == morse_units("=", 1) && morse_units("<KN>", 4) == morse_units("(", 1));
	// A space or a closing bracket ends it; an unclosed prosign runs to the end of the text
	CHECK(morse_units("<A R>", 5) == morse_units("A R", 3));
	CHECK(morse_units("A<>R", 4) == morse_units("AR", 2));
	CHECK(morse_units("<AR", 3) == 13);
	// Two prosigns in a row are two characters
	CHECK(morse_units("<AR><AR>", 8) == 13 + 3 + 13);
	// <AR> keyed at 20 wpm (dot 60 ms): .-.-. = 0-60, 120-300, 360-420, 480-660, 720-780
	uint32_t e[12];
	int n = edges("<AR>", 20, 0, e, 12);
	CHECK(n == 10);
	if (n == 10) {
		CHECK(e[0] == 0 && e[1] == 60 && e[2] == 120 && e[3] == 300 && e[4] == 360 && e[5] == 420 && e[6] == 480 &&
		      e[7] == 660 && e[8] == 720 && e[9] == 780);
	}
}

static void test_farnsworth(void)
{
	uint16_t c, w;
	// Standard spacing: eff 0, equal to or above the character speed
	morse_gap_ms(20, 0, &c, &w);
	CHECK(c == 180 && w == 420);
	morse_gap_ms(20, 20, &c, &w);
	CHECK(c == 180 && w == 420);
	morse_gap_ms(20, 30, &c, &w);
	CHECK(c == 180 && w == 420);
	// 20 wpm characters at 10 wpm overall: t_a = (1200 - 372) / 200 = 4.14 s; 3/19 of it 653.7 ms, 7/19 1525.3 ms
	morse_gap_ms(20, 10, &c, &w);
	CHECK(c == 654 && w == 1525);
	// 18 wpm characters at 5 wpm (the ARRL's slowest practice speed): t_a = (1080 - 186) / 90 = 9.9333 s
	morse_gap_ms(18, 5, &c, &w);
	CHECK(c == 1568 && w == 3660);
	// Speeds are clamped: below MORSE_WPM_MIN overall is MORSE_WPM_MIN
	uint16_t c2, w2;
	morse_gap_ms(20, 1, &c, &w);
	morse_gap_ms(20, MORSE_WPM_MIN, &c2, &w2);
	CHECK(c == c2 && w == w2);

	// "PARIS " at 20/10 takes a minute / 10: 31 units of elements and their gaps (1860 ms), 4 character gaps and a
	// word gap. The key ends after "PARIS" at 1860 + 4 * 654 = 4476 ms, the trailing word gap brings it to 6001 ms
	uint32_t e[40];
	int n = edges("PARIS", 20, 10, e, 40);
	CHECK(n == 28);	// 14 elements
	if (n == 28) {
		CHECK(e[1] == 60);	// elements keep the character speed: the first dot is 60 ms
		CHECK(e[27] == 4476);
		CHECK(e[27] + 1525 >= 5999 && e[27] + 1525 <= 6001);
	}
	// The same text at standard spacing ends at 1860 + 4 * 180 = 2580 ms (and 2580 + 420 = 3000 ms for "PARIS ")
	n = edges("PARIS", 20, 0, e, 40);
	CHECK(n == 28 && e[27] == 2580);
	// The word gap is stretched too: "E E" at 20/10 is dot, 1525 ms, dot
	n = edges("E E", 20, 10, e, 40);
	CHECK(n == 4 && e[1] == 60 && e[2] == 60 + 1525 && e[3] == 120 + 1525);
	// A prosign's inner gaps are element gaps, not stretched
	n = edges("<AR>", 20, 10, e, 40);
	CHECK(n == 10 && e[9] == 780);
}

static void test_long_text(void)
{
	// Built with MORSE_TEXT_MAX 160: a 100 character text is keyed whole, not cut at the CW ID's 40
	char text[101];
	for (int i = 0; i < 100; i++) {
		text[i] = 'E';
	}
	text[100] = '\0';
	CHECK(MORSE_TEXT_MAX == 160);
	CHECK(morse_units(text, 100) == 100 * 1 + 99 * 3);
	// Longer than the buffer: cut at MORSE_TEXT_MAX
	char big[200];
	memset(big, 'E', sizeof(big));
	CHECK(morse_units(big, 200) == 160 * 1 + 159 * 3);
}

// morse_ms(): what the keyer takes, from the first element to the end of the last
static void test_duration(void)
{
	uint32_t e[100];
	CHECK(morse_ms("PARIS", 5, 20, 0) == 2580);
	CHECK(morse_ms("PARIS", 5, 20, 10) == 4476);
	CHECK(morse_ms("E E", 3, 20, 10) == 60 + 1525 + 60);
	CHECK(morse_ms("<AR>", 4, 20, 10) == 780);
	CHECK(morse_ms("", 0, 20, 0) == 0);
	int n = edges("CQ CQ DE N0CALL", 25, 12, e, 100);	// 41 elements, 82 edges
	CHECK(n == 82 && e[n - 1] == morse_ms("CQ CQ DE N0CALL", 15, 25, 12));
}

// CW messages' text (radio_cwmsg.c): what is keyed, and {CALL}
static void test_cwmsg_text(void)
{
	char out[32];
	CHECK(radio_cwmsg_clean("  cq  de\tkn0ble <ar>  ", out, sizeof out) == 17 && strcmp(out, "CQ DE KN0BLE <AR>") == 0);
	CHECK(radio_cwmsg_clean("a#b ~ c", out, sizeof out) == 4 && strcmp(out, "AB C") == 0);	// no code: left out
	CHECK(radio_cwmsg_clean("### $$$", out, sizeof out) == 0 && out[0] == '\0');
	CHECK(radio_cwmsg_clean("ABCDEF GH", out, 8) == 6 && strcmp(out, "ABCDEF") == 0);	// full: cut at a character
	CHECK(radio_cwmsg_expand("CQ DE {CALL} {CALL} K", "KN0BLE", out, sizeof out) == 21 &&
	      strcmp(out, "CQ DE KN0BLE KN0BLE K") == 0);
	CHECK(radio_cwmsg_expand("DE {CALL}", "", out, sizeof out) == 0);		// needs a callsign
	CHECK(radio_cwmsg_expand("QRZ?", NULL, out, sizeof out) == 4);		// needs none
	CHECK(radio_cwmsg_expand("DE {CALL}", "KN0BLE", out, 8) == 0);		// doesn't fit
	// The handover: one message at a time, nothing to key refused
	radio_cwmsg_reset();
	radio_cwmsg_view_t v;
	CHECK(radio_cwmsg_put("test") && !radio_cwmsg_put("again"));
	CHECK(radio_cwmsg_take(out, sizeof out) == 4 && memcmp(out, "TEST", 4) == 0);
	radio_cwmsg_get(&v);
	CHECK(v.active);
	radio_cwmsg_ended(CWMSG_SENT, 1234);
	radio_cwmsg_get(&v);
	CHECK(!v.active && v.last == CWMSG_SENT && v.sent == 1 && v.last_ms == 1234);
	CHECK(!radio_cwmsg_put("#") && radio_cwmsg_take(out, sizeof out) == 0);
	radio_cwmsg_get(&v);
	CHECK(v.last == CWMSG_EMPTY && !v.active);
}

int main(void)
{
	test_duration();
	test_cwmsg_text();
	test_punctuation();
	test_prosigns();
	test_farnsworth();
	test_long_text();
	if (failures != 0) {
		printf("morseTest: %d failure(s)\n", failures);
		return 1;
	}
	printf("morseTest: all passed\n");
	return 0;
}
