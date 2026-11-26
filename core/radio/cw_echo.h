// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// What a hand-keyed session sent, as text (docs/spec/morse.md 10.5): the key's own edges classed against the keyer's
// dot. A mark of two dots or more is a dah; a gap of two dots ends a character, of five a word. No RTOS: the host
// tests run it.
#ifndef CW_ECHO_H
#define CW_ECHO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CW_ECHO_OUT_MAX	6u	// the most one call can give: a character ("<SK>" at most 4) and a space

typedef struct {
	uint16_t dot_ms;
	uint8_t code;		// morse.c's form: a marker bit, then the elements so far, MSB first (0 dot, 1 dah)
	uint8_t els;		// elements in code (more than 7: '*')
	bool key;		// the key now
	uint32_t edge_ms;	// its last edge
	bool word_done;		// this gap has given its space
	bool any;		// a character since the start (no space before the first)
} cw_echo_t;

void cw_echo_start(cw_echo_t *e, uint16_t dot_ms);
// The key went down or up at ms. Text the edge ends (a character before the press that starts the next) goes to out;
// its length
size_t cw_echo_edge(cw_echo_t *e, bool down, uint32_t ms, char *out, size_t max);
// Time passing with the key up: a character, then a word space, once the gap is long enough. Its length
size_t cw_echo_poll(cw_echo_t *e, uint32_t now_ms, char *out, size_t max);
// The session ended: the character in progress. Its length
size_t cw_echo_flush(cw_echo_t *e, char *out, size_t max);

#endif
