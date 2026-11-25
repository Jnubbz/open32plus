// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A semi-automatic key, a bug (docs/spec/morse.md 10): the dit paddle sends dits at the keyer's speed while held, each
// dit and its space whole; the dah paddle keys by hand, as a straight key. The paddles' edges, time-stamped where they
// happened, become the key's edges at their own times, for the straight key's delayed keying (cw_hand.h). No RTOS: the
// host tests run it.
#ifndef CW_BUG_H
#define CW_BUG_H

#include <stdint.h>
#include <stdbool.h>
#include "radio/cw_hand.h"

#define CW_BUG_EDGES	8u	// paddle edges queued until their time is run

typedef struct {
	uint32_t t[CW_BUG_EDGES];	// each queued paddle edge's time
	uint8_t what[CW_BUG_EDGES];	// bit 0 down, bit 1 the dah paddle
	uint8_t head, n;
	bool dit, dah;			// the paddles at `at`
	bool run;			// dits running, from run_ms
	uint32_t run_ms;
	bool key;			// the key made from them
	uint32_t at;			// run up to here
} cw_bug_t;

// Starts with nothing held; the first edge may be at ms or later
void cw_bug_start(cw_bug_t *b, uint32_t ms);
// A paddle went down or up at ms. False when the queue is full (the edge lost: the caller ends the session)
bool cw_bug_edge(cw_bug_t *b, bool dah, bool down, uint32_t ms);
// Run to upto: each change of the key goes to h at the millisecond it happens. dot_ms: the keyer's dot. False when h
// refused an edge (full)
bool cw_bug_run(cw_bug_t *b, uint32_t upto, uint32_t dot_ms, cw_hand_t *h);
// Nothing held, no dits running, nothing queued
bool cw_bug_idle(const cw_bug_t *b);

#endif
