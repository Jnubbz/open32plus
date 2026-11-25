// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A paddle keyer (docs/spec/morse.md 10): two paddles, a dit and a dah one, made into the key's edges at the keyer's
// speed. The modes:
//   iambic B  each paddle repeats its element while held; squeezed, they alternate, and an element the other paddle
//             was pressed during is sent after it (the memory), released or not
//   iambic A  the same without the memory: at each element's end the paddles held then decide
//   ultimatic squeezed, the paddle pressed last repeats
//   bug       the dit paddle repeats dits; the dah paddle keys by hand, the key down for as long as it is held
// Every element and its space run whole. The paddles' edges, time-stamped where they happened, become the key's edges
// at their own times for the straight key's delayed keying (cw_hand.h). No RTOS: the host tests run it.
#ifndef CW_KEYER_H
#define CW_KEYER_H

#include <stdint.h>
#include <stdbool.h>
#include "radio/cw_hand.h"

#define CW_KEYER_EDGES	8u	// paddle edges queued until their time is run

typedef enum {
	CW_KEYER_IAMBIC_B = 0,	// settings_t.cw_paddle 0: the default
	CW_KEYER_IAMBIC_A,
	CW_KEYER_ULTIMATIC,
	CW_KEYER_BUG,
	CW_KEYER_MODES
} cw_keyer_mode_t;

typedef struct {
	uint32_t t[CW_KEYER_EDGES];	// each queued paddle edge's time
	uint8_t what[CW_KEYER_EDGES];	// bit 0 down, bit 1 the dah paddle
	uint8_t head, n;
	uint8_t mode;			// cw_keyer_mode_t
	bool dit, dah;			// the paddles at `at`
	bool last_dah;			// ultimatic: the paddle pressed last
	bool run;			// an element and its space running, from el_ms
	bool el_dah;			// ... a dah (iambic, ultimatic) or a dit
	bool mem_dit, mem_dah;		// iambic B: the paddle pressed during the element, sent next
	uint32_t el_ms;
	bool key;			// the key made from them
	uint32_t at;			// run up to here
} cw_keyer_t;

// Starts with nothing held; the first edge may be at ms or later. mode: cw_keyer_mode_t (others: iambic B)
void cw_keyer_start(cw_keyer_t *k, uint8_t mode, uint32_t ms);
// A paddle went down or up at ms. False when the queue is full (the edge lost: the caller ends the session)
bool cw_keyer_edge(cw_keyer_t *k, bool dah, bool down, uint32_t ms);
// Run to upto: each change of the key goes to h at the millisecond it happens. dot_ms: the keyer's dot. False when h
// refused an edge (full)
bool cw_keyer_run(cw_keyer_t *k, uint32_t upto, uint32_t dot_ms, cw_hand_t *h);
// Nothing held, no element running, nothing queued or remembered
bool cw_keyer_idle(const cw_keyer_t *k);

#endif
