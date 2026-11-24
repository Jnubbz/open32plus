// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The CW decoder's radio side (radio_cwdec.h, docs/spec/morse.md 7)
#include "config.h"
#if CONFIG_CW_DECODE
#include <string.h>
#include "board.h"
#include "radio/morse_decode.h"
#include "radio/radio_cwdec.h"

#define RETRY_MS	1000u	// a start the board refused (no memory, the path busy) is tried again this much later
#define READ_N		80u	// samples per read: one block

static volatile bool want, reset_req, clear_req;
static bool running, retry_wait, open_was;
static uint32_t retry_ms;
static morse_dec_t dec;
static int16_t rxbuf[READ_N];

// The text: written by the radio task only, read by the UI under the sequence count (odd while it changes)
static char text[CWDEC_TEXT_MAX];
static size_t text_n;
static volatile uint32_t seq;
static volatile uint8_t state;
static volatile uint16_t hz;
static volatile uint8_t wpm;
static volatile uint32_t chars;

void radio_cwdec_want(bool on)
{
	if (on && !want) {
		reset_req = true;
	}
	want = on;
}

void radio_cwdec_clear(void)
{
	clear_req = true;
}

void radio_cwdec_get(radio_cwdec_view_t *v, char *out, size_t max)
{
	for (int tries = 0; tries < 8; tries++) {
		uint32_t s1 = seq;
		if (s1 & 1u) {
			continue;
		}
		v->state = state;
		v->hz = hz;
		v->wpm = wpm;
		v->chars = chars;
		v->serial = s1;
		if (out != NULL && max != 0) {
			size_t n = text_n, from = n + 1u > max ? n + 1u - max : 0u;
			memcpy(out, text + from, n - from);
			out[n - from] = 0;
		}
		if (seq == s1) {
			return;
		}
	}
	if (out != NULL && max != 0) {
		out[0] = 0;	// the radio task kept writing: nothing torn
	}
}

static bool path_ok(void)
{
	const hal_afsk_t *h = board.afsk;
	return h != NULL && h->rx_start != NULL && h->rx_read != NULL && h->rx_stop != NULL && h->rx_rate_hz == MDEC_RATE;
}

static void text_begin(void)
{
	seq++;
}

static void text_end(void)
{
	seq++;
}

// Decoded text in; the oldest third goes when it fills (radio task, between text_begin and text_end)
static void append(const char *t, size_t n)
{
	if (n == 0) {
		return;
	}
	if (text_n + n >= CWDEC_TEXT_MAX) {
		size_t drop = CWDEC_TEXT_MAX / 3u;
		drop = drop > text_n ? text_n : drop;
		memmove(text, text + drop, text_n - drop);
		text_n -= drop;
	}
	if (n > CWDEC_TEXT_MAX - 1u - text_n) {
		n = CWDEC_TEXT_MAX - 1u - text_n;
	}
	memcpy(text + text_n, t, n);
	text_n += n;
}

static void stop(void)
{
	if (running) {
		board.afsk->rx_stop();
		running = false;
	}
	if (open_was) {
		char out[MDEC_OUT_MAX];
		size_t k = morse_dec_flush(&dec, out, sizeof out);
		text_begin();
		append(out, k);
		text_end();
		open_was = false;
	}
}

void radio_cwdec_release(void)
{
	stop();
}

bool radio_cwdec_running(void)
{
	return running;
}

bool radio_cwdec_service(const radio_cwdec_in_t *in, uint32_t now_ms)
{
	bool changed = false;
	if (reset_req || clear_req) {
		if (reset_req) {
			morse_dec_init(&dec);
			chars = 0;
		}
		reset_req = clear_req = false;
		text_begin();
		text_n = 0;
		text_end();
		changed = true;
	}
	uint8_t st = !want ? CWDEC_OFF : !path_ok() ? CWDEC_NO_PATH : !in->fm ? CWDEC_NOT_FM :
		     in->tx || in->busy ? CWDEC_BUSY : in->rx_open ? CWDEC_HEARING : CWDEC_LISTENING;
	bool can = st == CWDEC_LISTENING || st == CWDEC_HEARING;
	if (can && !running && (!retry_wait || (int32_t)(now_ms - retry_ms) >= 0)) {
		running = board.afsk->rx_start();
		retry_wait = !running;
		retry_ms = now_ms + RETRY_MS;
	} else if (!can) {
		stop();
		retry_wait = false;
	}
	if (running) {
		uint16_t k;
		while ((k = board.afsk->rx_read(rxbuf, READ_N)) != 0) {
			if (!in->rx_open) {
				continue;	// the squelch closed: noise, not Morse
			}
			char out[MDEC_OUT_MAX * 2u];
			size_t n = morse_dec_feed(&dec, rxbuf, k, out, sizeof out);
			if (n != 0) {
				text_begin();
				append(out, n);
				text_end();
				chars += n;
				changed = true;
			}
			open_was = true;
		}
		if (!in->rx_open && open_was) {
			char out[MDEC_OUT_MAX];
			size_t n = morse_dec_flush(&dec, out, sizeof out);
			text_begin();
			append(out, n);
			text_end();
			open_was = false;
			changed = changed || n != 0;
		}
	}
	uint16_t h = morse_dec_hz(&dec);
	uint8_t w = h != 0 ? morse_dec_wpm(&dec) : 0;
	if (st != state || h != hz || w != wpm) {
		state = st;
		hz = h;
		wpm = w;
		changed = true;
	}
	return changed;
}
#endif
