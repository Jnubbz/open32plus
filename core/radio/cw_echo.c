// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
#include "radio/cw_echo.h"
#include "radio/morse_decode.h"

void cw_echo_start(cw_echo_t *e, uint16_t dot_ms)
{
	e->dot_ms = dot_ms != 0 ? dot_ms : 60u;
	e->code = 1;
	e->els = 0;
	e->key = false;
	e->edge_ms = 0;
	e->word_done = true;
	e->any = false;
}

// The character heard so far, if any, to out
static size_t char_out(cw_echo_t *e, char *out, size_t max)
{
	if (e->els == 0 || max < 4u) {
		return 0;
	}
	size_t n;
	if (e->els > 7u) {
		out[0] = '*';
		n = 1;
	} else {
		n = morse_char(e->code, out);
	}
	e->code = 1;
	e->els = 0;
	e->any = true;
	e->word_done = false;
	return n;
}

size_t cw_echo_poll(cw_echo_t *e, uint32_t now_ms, char *out, size_t max)
{
	if (e->key) {
		return 0;
	}
	uint32_t gap = now_ms - e->edge_ms;
	size_t n = 0;
	if (gap >= 2u * e->dot_ms) {
		n = char_out(e, out, max);
	}
	if (gap >= 5u * e->dot_ms && !e->word_done && e->any && n < max) {
		out[n++] = ' ';
		e->word_done = true;
	}
	return n;
}

size_t cw_echo_edge(cw_echo_t *e, bool down, uint32_t ms, char *out, size_t max)
{
	if (down == e->key) {
		return 0;
	}
	size_t n = 0;
	if (down) {
		n = cw_echo_poll(e, ms, out, max);	// the gap that ends here, as time had told it
	} else {
		uint32_t mark = ms - e->edge_ms;
		if (e->els < 7u) {
			e->code = (uint8_t)((e->code << 1) | (mark >= 2u * e->dot_ms ? 1u : 0u));
		}
		e->els++;
	}
	e->key = down;
	e->edge_ms = ms;
	return n;
}

size_t cw_echo_flush(cw_echo_t *e, char *out, size_t max)
{
	return e->key ? 0 : char_out(e, out, max);
}
