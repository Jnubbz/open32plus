// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A semi-automatic key (cw_bug.h, docs/spec/morse.md 10)
#include <string.h>
#include "radio/cw_bug.h"

#define RUN_MAX_MS	5000u	// the most run in one call: a late call catches up in steps

void cw_bug_start(cw_bug_t *b, uint32_t ms)
{
	memset(b, 0, sizeof *b);
	b->at = ms - 1u;	// an edge at ms is still ahead
}

bool cw_bug_edge(cw_bug_t *b, bool dah, bool down, uint32_t ms)
{
	if (b->n == CW_BUG_EDGES) {
		return false;
	}
	uint8_t i = (uint8_t)((b->head + b->n) % CW_BUG_EDGES);
	b->t[i] = ms;
	b->what[i] = (uint8_t)((down ? 1u : 0u) | (dah ? 2u : 0u));
	b->n++;
	return true;
}

bool cw_bug_run(cw_bug_t *b, uint32_t upto, uint32_t dot_ms, cw_hand_t *h)
{
	if (dot_ms == 0) {
		dot_ms = 1;
	}
	uint32_t steps = upto - b->at;
	if ((int32_t)steps <= 0) {
		return true;
	}
	if (steps > RUN_MAX_MS) {
		steps = RUN_MAX_MS;
	}
	for (uint32_t s = 0; s < steps; s++) {
		uint32_t t = ++b->at;
		// The paddles as of t: every edge due by now (one later than its time if it came late)
		while (b->n != 0 && (int32_t)(t - b->t[b->head]) >= 0) {
			uint8_t w = b->what[b->head];
			if (w & 2u) {
				b->dah = (w & 1u) != 0;
			} else {
				b->dit = (w & 1u) != 0;
			}
			b->head = (uint8_t)((b->head + 1u) % CW_BUG_EDGES);
			b->n--;
		}
		// Dits: one starts when the paddle goes down, and each dit and its space run whole. At the end of a space
		// the next starts only if the paddle is still held
		if (!b->run && b->dit) {
			b->run = true;
			b->run_ms = t;
		}
		bool on = false;
		if (b->run) {
			uint32_t ph = (t - b->run_ms) % (2u * dot_ms);
			if (ph == 0 && t != b->run_ms && !b->dit) {
				b->run = false;
			} else {
				on = ph < dot_ms;
			}
		}
		bool key = b->dah || on;
		if (key != b->key) {
			b->key = key;
			if (!cw_hand_edge(h, key, t)) {
				return false;
			}
		}
	}
	return true;
}

bool cw_bug_idle(const cw_bug_t *b)
{
	return !b->dit && !b->dah && !b->run && b->n == 0;
}
