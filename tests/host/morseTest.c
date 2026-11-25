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
#include "radio/morse_decode.h"
#include "radio/cw_hand.h"
#include "radio/cw_qso.h"
#include "radio/cw_bug.h"
#include <math.h>
#include <stdlib.h>

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


// ---- The decoder (morse_decode.c, morse.md 7): MCW audio made with the keyer itself, 8 kHz, decoded back ----

static uint32_t noise_seed = 12345u;

static double gauss(void)
{
	double u = 0;
	for (int i = 0; i < 12; i++) {
		noise_seed = noise_seed * 1103515245u + 12345u;
		u += (double)((noise_seed >> 8) & 0xFFFF) / 65536.0;
	}
	return u - 6.0;		// about unit variance
}

// text keyed at wpm (eff: Farnsworth, 0 off) as a hz tone of amp with 5 ms edges and noise of sigma, after lead_ms of
// noise alone; decoded into out
static void mcw_decode(const char *text, uint8_t wpm, uint8_t eff, uint16_t hz, double amp, double sigma,
		       uint32_t lead_ms, char *out, size_t max, morse_dec_t *d)
{
	morse_t m;
	morse_start_farnsworth(&m, text, (uint8_t)strlen(text), wpm, eff, lead_ms);
	morse_dec_init(d);
	size_t n = 0;
	double env = 0, ph = 0;
	int16_t blk[8];
	for (uint32_t ms = 0; !m.done || ms < lead_ms + 100u; ms++) {
		bool key = morse_poll(&m, ms);
		for (int i = 0; i < 8; i++) {
			env += key ? 1.0 / 40 : -1.0 / 40;
			env = env < 0 ? 0 : env > 1 ? 1 : env;
			double e = (1 - cos(M_PI * env)) / 2;
			double v = amp * e * sin(ph) + sigma * gauss();
			ph += 2 * M_PI * hz / 8000.0;
			blk[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
		n += morse_dec_feed(d, blk, 8, out + n, max - 1 - n);
		if (ms > 600000u) {
			break;
		}
	}
	n += morse_dec_flush(d, out + n, max - 1 - n);
	out[n] = 0;
}

static void test_decode(void)
{
	char out[200];
	morse_dec_t d;
	// every character the keyer knows comes back
	char t[4];
	const char *all = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-=?.,+@'():\"!";
	for (const char *p = all; *p; p++) {
		CHECK(morse_char(morse_code(*p), t) == 1 && t[0] == *p);
	}
	CHECK(morse_char(0x45, t) == 4 && memcmp(t, "<SK>", 4) == 0);
	CHECK(morse_char(0x7F, t) == 1 && t[0] == '*');

	// 20 wpm at 800 Hz, clean
	mcw_decode("CQ CQ DE KN0BLE K", 20, 0, 800, 8000, 0, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "CQ CQ DE KN0BLE K ") == 0);
	CHECK(morse_dec_hz(&d) == 800 && morse_dec_wpm(&d) >= 19 && morse_dec_wpm(&d) <= 21);
	if (strcmp(out, "CQ CQ DE KN0BLE K ") != 0) printf("  got \"%s\"\n", out);
	// slow and low, fast and high, each with noise about 12 dB under the tone
	mcw_decode("TEST DE KN0BLE", 5, 0, 600, 8000, 1400, 500, out, sizeof out, &d);
	CHECK(strcmp(out, "TEST DE KN0BLE ") == 0);
	if (strcmp(out, "TEST DE KN0BLE ") != 0) printf("  got \"%s\"\n", out);
	CHECK(morse_dec_hz(&d) == 600 && morse_dec_wpm(&d) >= 4 && morse_dec_wpm(&d) <= 6);
	mcw_decode("QRZ? DE KN0BLE 73 <SK>", 35, 0, 950, 8000, 1400, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "QRZ? DE KN0BLE 73 <SK> ") == 0);
	if (strcmp(out, "QRZ? DE KN0BLE 73 <SK> ") != 0) printf("  got \"%s\"\n", out);
	CHECK(morse_dec_hz(&d) == 950);
	// the ends of the range lock between the guard bins
	mcw_decode("TEST 73", 20, 0, 400, 8000, 800, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "TEST 73 ") == 0 && morse_dec_hz(&d) == 400);
	if (strcmp(out, "TEST 73 ") != 0) printf("  got \"%s\"\n", out);
	mcw_decode("TEST 73", 20, 0, 1000, 8000, 800, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "TEST 73 ") == 0 && morse_dec_hz(&d) == 1000);
	if (strcmp(out, "TEST 73 ") != 0) printf("  got \"%s\"\n", out);
	// a tone between bins (725 Hz) still locks and decodes
	mcw_decode("PARIS PARIS", 18, 0, 725, 8000, 800, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "PARIS PARIS ") == 0);
	if (strcmp(out, "PARIS PARIS ") != 0) printf("  got \"%s\"\n", out);
	// Farnsworth 20/10: the first character gaps read as word gaps until two one-letter words show what they are;
	// the words after are whole
	mcw_decode("CQ CQ CQ DE KN0BLE KN0BLE K", 20, 10, 800, 8000, 600, 300, out, sizeof out, &d);
	const char *tail = strstr(out, "DE KN0BLE KN0BLE K ");
	CHECK(tail != NULL);
	if (tail == NULL) printf("  got \"%s\"\n", out);
	// weak: the tone 1 dB over the noise in the 4 kHz band
	noise_seed = 1u;
	mcw_decode("THE QUICK BROWN FOX 0123456789", 20, 0, 800, 8000, 5000, 300, out, sizeof out, &d);
	CHECK(strcmp(out, "THE QUICK BROWN FOX 0123456789 ") == 0);
	if (strcmp(out, "THE QUICK BROWN FOX 0123456789 ") != 0) printf("  got \"%s\"\n", out);
	// another station after the squelch closed, slower and lower: learned again from the first elements
	{
		morse_t m;
		morse_dec_t e;
		morse_dec_init(&e);
		size_t n = 0;
		const char *texts[2] = { "CQ CQ DE KN0BLE K", "KN0BLE DE W1AW K" };
		const uint8_t wpm[2] = { 25, 12 };
		const uint16_t hz[2] = { 800, 650 };
		for (int k = 0; k < 2; k++) {
			morse_start(&m, texts[k], (uint8_t)strlen(texts[k]), wpm[k], 300);
			double env = 0, ph = 0;
			int16_t blk[8];
			for (uint32_t ms = 0; !m.done || ms < 400u; ms++) {
				bool key = morse_poll(&m, ms);
				for (int i = 0; i < 8; i++) {
					env += key ? 1.0 / 40 : -1.0 / 40;
					env = env < 0 ? 0 : env > 1 ? 1 : env;
					blk[i] = (int16_t)(8000 * (1 - cos(M_PI * env)) / 2 * sin(ph) + 800 * gauss());
					ph += 2 * M_PI * hz[k] / 8000.0;
				}
				n += morse_dec_feed(&e, blk, 8, out + n, sizeof out - 1 - n);
			}
			n += morse_dec_flush(&e, out + n, sizeof out - 1 - n);	// the squelch closes between the overs
		}
		out[n] = 0;
		CHECK(strcmp(out, "CQ CQ DE KN0BLE K KN0BLE DE W1AW K ") == 0);
		if (strcmp(out, "CQ CQ DE KN0BLE K KN0BLE DE W1AW K ") != 0) printf("  got \"%s\"\n", out);
		CHECK(morse_dec_hz(&e) == 650 && morse_dec_wpm(&e) >= 11 && morse_dec_wpm(&e) <= 13);
	}
	// on the air (991A MCW, 600 Hz, 16 wpm): between the elements the receiver's noise rose, strongest low after
	// de-emphasis. It must neither lock (its top bin is the lowest) nor move the lock in a gap, which cut every
	// element short by the blocks a new lock takes (16 wpm read as 27)
	{
		morse_t m;
		morse_dec_t e;
		const char *text = "CQ CQ DE KN0BLE K";
		morse_start(&m, text, (uint8_t)strlen(text), 16, 300);
		morse_dec_init(&e);
		size_t n = 0;
		double env = 0, ph = 0, lp = 0, lp2 = 0, hp = 0;
		int16_t blk[8];
		noise_seed = 7u;
		for (uint32_t ms = 0; !m.done || ms < 600u; ms++) {
			bool key = morse_poll(&m, ms);
			for (int i = 0; i < 8; i++) {
				env += key ? 1.0 / 40 : -1.0 / 40;
				env = env < 0 ? 0 : env > 1 ? 1 : env;
				double g = (1 - cos(M_PI * env)) / 2;
				// white noise through two poles near 280 Hz and the receiver's high-pass under 300 Hz
				lp += 0.2 * (gauss() - lp);
				lp2 += 0.2 * (lp - lp2);
				hp += 0.2 * (lp2 - hp);
				double v = 8000 * g * sin(ph) + 30000 * (1 - g) * (lp2 - hp) + 300 * gauss();
				ph += 2 * M_PI * 600 / 8000.0;
				blk[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
			}
			n += morse_dec_feed(&e, blk, 8, out + n, sizeof out - 1 - n);
		}
		n += morse_dec_flush(&e, out + n, sizeof out - 1 - n);
		out[n] = 0;
		CHECK(strcmp(out, "CQ CQ DE KN0BLE K ") == 0);
		if (strcmp(out, "CQ CQ DE KN0BLE K ") != 0) printf("  got \"%s\" at %u Hz %u wpm\n", out,
			(unsigned)morse_dec_hz(&e), (unsigned)morse_dec_wpm(&e));
		CHECK(morse_dec_hz(&e) == 600 && morse_dec_wpm(&e) >= 15 && morse_dec_wpm(&e) <= 17);
	}
	// noise alone: nothing decoded
	mcw_decode("", 20, 0, 800, 0, 2000, 3000, out, sizeof out, &d);
	CHECK(strlen(out) <= 2);
	if (strlen(out) > 2) printf("  got \"%s\"\n", out);
}


// ---- The straight key (cw_hand.c, morse.md 8): edges played a lead-in later, the carrier held through the gaps ----

static void test_hand(void)
{
	cw_hand_t h;
	bool done;
	const uint32_t lead = 600, hang = 1000;
	cw_hand_start(&h, 1000);
	// a dot pressed at 1100 for 60 ms, a dash at 1220 for 180 ms; a bouncing repeat changes nothing
	CHECK(cw_hand_edge(&h, true, 1100) && cw_hand_edge(&h, true, 1105) && cw_hand_edge(&h, false, 1160));
	CHECK(cw_hand_edge(&h, true, 1220) && cw_hand_edge(&h, false, 1400));
	// silent carrier through the lead-in, while the key is long released: not done
	CHECK(!cw_hand_poll(&h, 1650, lead, hang, &done) && !done);
	// each element at its own time plus the lead-in, its length kept to the millisecond
	CHECK(!cw_hand_poll(&h, 1699, lead, hang, &done));
	CHECK(cw_hand_poll(&h, 1700, lead, hang, &done));
	CHECK(cw_hand_poll(&h, 1759, lead, hang, &done));
	CHECK(!cw_hand_poll(&h, 1760, lead, hang, &done) && !done);
	CHECK(cw_hand_poll(&h, 1820, lead, hang, &done) && cw_hand_poll(&h, 1999, lead, hang, &done));
	CHECK(!cw_hand_poll(&h, 2000, lead, hang, &done) && !done);
	// the carrier hangs a second past the last release on the air, then the session is done
	CHECK(!cw_hand_poll(&h, 2999, lead, hang, &done) && !done);
	CHECK(!cw_hand_poll(&h, 3000, lead, hang, &done) && done);
	// a press during the hang holds the carrier: not done while it is queued or held
	cw_hand_start(&h, 0);
	CHECK(cw_hand_edge(&h, true, 10) && cw_hand_edge(&h, false, 70));
	CHECK(!cw_hand_poll(&h, 1600, lead, hang, &done) && !done);
	CHECK(cw_hand_edge(&h, true, 1650));
	CHECK(!cw_hand_poll(&h, 1700, lead, hang, &done) && !done);	// 1030 ms past the release, but a press is queued
	CHECK(cw_hand_poll(&h, 2250, lead, hang, &done) && !done);
	CHECK(cw_hand_edge(&h, false, 1700) && !cw_hand_poll(&h, 2300, lead, hang, &done) && !done);
	CHECK(!cw_hand_poll(&h, 3299, lead, hang, &done) && !done && !cw_hand_poll(&h, 3300, lead, hang, &done) && done);
	// held for ever: the air key stays down (the time-out cuts it, not the hang)
	cw_hand_start(&h, 0);
	CHECK(cw_hand_edge(&h, true, 0));
	CHECK(cw_hand_poll(&h, 60000, lead, hang, &done) && !done);
	// a full queue refuses the edge
	cw_hand_start(&h, 0);
	bool ok = true;
	for (uint32_t i = 0; i < CW_HAND_EDGES; i++) {
		ok = ok && cw_hand_edge(&h, (i & 1u) == 0, i * 10u);
	}
	CHECK(ok && !cw_hand_edge(&h, true, 1000));
	// the clock wrapping in the middle keeps the timing
	cw_hand_start(&h, 0xFFFFFF00u);
	CHECK(cw_hand_edge(&h, true, 0xFFFFFF80u) && cw_hand_edge(&h, false, 0xFFFFFFE0u));
	CHECK(cw_hand_poll(&h, 0xFFFFFF80u + lead, lead, hang, &done));
	CHECK(!cw_hand_poll(&h, 0xFFFFFFE0u + lead, lead, hang, &done) && !done);
	CHECK(!cw_hand_poll(&h, 0xFFFFFFE0u + lead + hang, lead, hang, &done) && done);
}

// ---- The QSO helper (cw_qso.c, morse.md 9): callsigns out of decoded CW, {THEM} filled in ----

static bool is_call(const char *w)
{
	bool d;
	return cw_qso_is_call(w, strlen(w), &d);
}

static void test_qso(void)
{
	// Calls: one or two prefix characters with a letter, a digit, one to four suffix letters, '/' parts
	const char *calls[] = { "W1AW", "KN0BLE", "VE3XYZ", "2E0ABC", "9A1A", "JA1XYZ", "K1A", "KH6/W1AW", "W1AW/P",
		"W1AW/QRP", "W1AW/4", "E51ABC", "HB9XYZ" };
	for (unsigned i = 0; i < sizeof calls / sizeof calls[0]; i++) {
		CHECK(is_call(calls[i]));
		if (!is_call(calls[i])) printf("  not a call: %s\n", calls[i]);
	}
	// The CW around them is not
	const char *words[] = { "CQ", "DE", "K", "599", "5NN", "73", "TU", "UR5", "QRZ?", "R5", "1ST", "<SK>", "EE", "BK",
		"FL", "QTH", "W1ABCDE", "W1AW/QRPPP", "WW", "/W1AW", "W1AW/", "**AW", "TEST", "1234" };
	for (unsigned i = 0; i < sizeof words / sizeof words[0]; i++) {
		CHECK(!is_call(words[i]));
		if (is_call(words[i])) printf("  taken for a call: %s\n", words[i]);
	}
	bool d = false;
	CHECK(cw_qso_is_call("W1A*", 4, &d) && d);
	CHECK(cw_qso_is_call("W1AW", 4, &d) && !d);

	cw_qso_call_t c[CW_QSO_CALLS];
	// An answer to a CQ: the caller heard twice comes first, our own call never
	uint8_t n = cw_qso_calls("CQ CQ DE KN0BLE KN0BLE K KN0BLE DE W1AW W1AW K", "KN0BLE", c);
	CHECK(n == 1 && strcmp(c[0].call, "W1AW") == 0 && c[0].heard == 2 && !c[0].doubtful);
	// Newest first; heard more than once ahead of once
	n = cw_qso_calls("K1ABC DE N2XYZ K KN0BLE DE W1AW K VE3XYZ", "KN0BLE", c);
	CHECK(n == 4 && strcmp(c[0].call, "VE3XYZ") == 0 && strcmp(c[1].call, "W1AW") == 0 &&
	      strcmp(c[2].call, "N2XYZ") == 0 && strcmp(c[3].call, "K1ABC") == 0);
	n = cw_qso_calls("W1AW W1AW DE N2XYZ", "", c);
	CHECK(n == 2 && strcmp(c[0].call, "W1AW") == 0 && strcmp(c[1].call, "N2XYZ") == 0);
	// A doubtful copy that fits a clean call counts for it; one that fits none is offered last, marked
	n = cw_qso_calls("W1A* DE W1AW K N2X*Z", "KN0BLE", c);
	CHECK(n == 2 && strcmp(c[0].call, "W1AW") == 0 && c[0].heard == 2 && strcmp(c[1].call, "N2X*Z") == 0 &&
	      c[1].doubtful);
	// ... and a doubtful copy of our own call is ours
	n = cw_qso_calls("KN0B*E DE W1AW", "KN0BLE", c);
	CHECK(n == 1 && strcmp(c[0].call, "W1AW") == 0);
	// At most five, the newest; nothing in plain CW
	n = cw_qso_calls("K1A K2A K3A K4A K5A K6A K7A", "", c);
	CHECK(n == 5 && strcmp(c[0].call, "K7A") == 0 && strcmp(c[4].call, "K3A") == 0);
	CHECK(cw_qso_calls("CQ CQ TEST 599 TU 73 <SK>", "", c) == 0);
	CHECK(cw_qso_calls("", "KN0BLE", c) == 0 && cw_qso_calls("   ", NULL, c) == 0);

	// {THEM} filled in, every time; too long is refused
	char out[40];
	CHECK(cw_qso_fill("{THEM} DE {CALL} UR 599 599 BK", "W1AW", out, sizeof out) == 28 &&
	      strcmp(out, "W1AW DE {CALL} UR 599 599 BK") == 0);
	CHECK(cw_qso_fill("{THEM} {THEM} DE {CALL} K", "KH6/W1AW", out, sizeof out) == 29);
	CHECK(cw_qso_fill("TU 73", "W1AW", out, sizeof out) == 5 && strcmp(out, "TU 73") == 0);
	CHECK(cw_qso_fill("{THEM} {THEM} {THEM} {THEM}", "KH6/W1AW/QRP", out, sizeof out) == 0);
	CHECK(cw_qso_fill("{THEM}", "W1AW", out, 5) == 4 && cw_qso_fill("{THEM}", "W1AW", out, 4) == 0);
}

// ---- The bug (cw_bug.c, morse.md 10): the dit paddle's dits at the keyer's speed, each whole; the dah by hand ----

// The key's edges the bug made, as "D<ms> U<ms> ..." from the delay line's queue
static void bug_edges(const cw_hand_t *h, char *out, size_t max)
{
	size_t n = 0;
	out[0] = 0;
	for (uint8_t k = 0; k < h->n; k++) {
		uint8_t i = (uint8_t)((h->head + k) % CW_HAND_EDGES);
		n += (size_t)snprintf(out + n, max - n, "%s%c%u", n ? " " : "", ((h->down >> i) & 1u) ? 'D' : 'U',
				      (unsigned)h->t[i]);
	}
}

static void test_bug(void)
{
	cw_bug_t b;
	cw_hand_t h;
	char e[200];
	const uint32_t dot = 60;	// 20 wpm
	// The dit paddle held 250 ms: dits at 1000, 1120, 1240 (still held at 1240), each 60 ms, then it stops at 1360
	cw_bug_start(&b, 1000);
	cw_hand_start(&h, 1000);
	CHECK(cw_bug_edge(&b, false, true, 1000) && cw_bug_edge(&b, false, false, 1250));
	CHECK(cw_bug_run(&b, 2000, dot, &h));
	bug_edges(&h, e, sizeof e);
	CHECK(strcmp(e, "D1000 U1060 D1120 U1180 D1240 U1300") == 0);
	if (strcmp(e, "D1000 U1060 D1120 U1180 D1240 U1300") != 0) printf("  got %s\n", e);
	CHECK(cw_bug_idle(&b));
	// A tap shorter than a dit still makes one whole dit; the dah is as long as it is held
	cw_bug_start(&b, 0);
	cw_hand_start(&h, 0);
	CHECK(cw_bug_edge(&b, false, true, 100) && cw_bug_edge(&b, false, false, 120));
	CHECK(cw_bug_edge(&b, true, true, 400) && cw_bug_edge(&b, true, false, 637));
	CHECK(cw_bug_run(&b, 1000, dot, &h));
	bug_edges(&h, e, sizeof e);
	CHECK(strcmp(e, "D100 U160 D400 U637") == 0);
	if (strcmp(e, "D100 U160 D400 U637") != 0) printf("  got %s\n", e);
	// Run in pieces as the radio core does, with an edge arriving after its time was passed: the same key
	cw_bug_start(&b, 0);
	cw_hand_start(&h, 0);
	CHECK(cw_bug_edge(&b, false, true, 10));
	for (uint32_t t = 10; t <= 200; t += 10) {
		CHECK(cw_bug_run(&b, t, dot, &h));
	}
	CHECK(cw_bug_edge(&b, false, false, 190));	// released at 190, heard at 200: the third dit at 250 doesn't start
	for (uint32_t t = 210; t <= 400; t += 10) {
		CHECK(cw_bug_run(&b, t, dot, &h));
	}
	bug_edges(&h, e, sizeof e);
	CHECK(strcmp(e, "D10 U70 D130 U190") == 0);
	if (strcmp(e, "D10 U70 D130 U190") != 0) printf("  got %s\n", e);
	CHECK(cw_bug_idle(&b));
	// Both paddles: the dah holds the key down through the dits; the dits go on after it
	cw_bug_start(&b, 0);
	cw_hand_start(&h, 0);
	CHECK(cw_bug_edge(&b, false, true, 0) && cw_bug_edge(&b, true, true, 30) && cw_bug_edge(&b, true, false, 200) &&
	      cw_bug_edge(&b, false, false, 250));
	CHECK(cw_bug_run(&b, 1000, dot, &h));
	bug_edges(&h, e, sizeof e);
	CHECK(strcmp(e, "D0 U200 D240 U300") == 0);
	if (strcmp(e, "D0 U200 D240 U300") != 0) printf("  got %s\n", e);
	// Held: not idle; a full paddle queue refuses
	cw_bug_start(&b, 0);
	cw_hand_start(&h, 0);
	CHECK(cw_bug_edge(&b, true, true, 0) && cw_bug_run(&b, 50, dot, &h) && !cw_bug_idle(&b));
	cw_bug_start(&b, 0);
	bool ok = true;
	for (unsigned i = 0; i < CW_BUG_EDGES; i++) {
		ok = ok && cw_bug_edge(&b, true, (i & 1u) == 0, i);
	}
	CHECK(ok && !cw_bug_edge(&b, true, true, 99));
}

int main(void)
{
	test_duration();
	test_cwmsg_text();
	test_punctuation();
	test_prosigns();
	test_farnsworth();
	test_long_text();
	test_decode();
	test_hand();
	test_qso();
	test_bug();
	if (failures != 0) {
		printf("morseTest: %d failure(s)\n", failures);
		return 1;
	}
	printf("morseTest: all passed\n");
	return 0;
}
