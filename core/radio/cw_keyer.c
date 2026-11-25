// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A paddle keyer (cw_keyer.h, docs/spec/morse.md 10)
#include <string.h>
#include "radio/cw_keyer.h"

#define RUN_MAX_MS	5000u	// the most run in one call: a late call catches up in steps

void cw_keyer_start(cw_keyer_t *k, uint8_t mode, uint32_t ms)
{
	memset(k, 0, sizeof *k);
	k->mode = mode < CW_KEYER_MODES ? mode : CW_KEYER_IAMBIC_B;
	k->at = ms - 1u;	// an edge at ms is still ahead
}

bool cw_keyer_edge(cw_keyer_t *k, bool dah, bool down, uint32_t ms)
{
	if (k->n == CW_KEYER_EDGES) {
		return false;
	}
	uint8_t i = (uint8_t)((k->head + k->n) % CW_KEYER_EDGES);
	k->t[i] = ms;
	k->what[i] = (uint8_t)((down ? 1u : 0u) | (dah ? 2u : 0u));
	k->n++;
	if ((int32_t)(k->at - ms) > 0 && k->late != UINT16_MAX) {
		k->late++;
	}
	return true;
}

// The paddles as of t: every edge due by now (one later than its time if it came late). A press during an element
// is remembered for iambic B
static void paddles(cw_keyer_t *k, uint32_t t)
{
	while (k->n != 0 && (int32_t)(t - k->t[k->head]) >= 0) {
		uint8_t w = k->what[k->head];
		bool down = (w & 1u) != 0;
		if (w & 2u) {
			if (down && !k->dah) {
				k->last_dah = true;
				k->mem_dah = k->mem_dah || (k->run && !k->el_dah);
			}
			k->dah = down;
		} else {
			if (down && !k->dit) {
				k->last_dah = false;
				k->mem_dit = k->mem_dit || (k->run && k->el_dah);
			}
			k->dit = down;
		}
		k->head = (uint8_t)((k->head + 1u) % CW_KEYER_EDGES);
		k->n--;
	}
}

// The element to start now, nothing running: 0 none, 1 a dit, 2 a dah
static uint8_t next(cw_keyer_t *k, bool after)
{
	bool dit = k->dit, dah = k->dah;
	if (k->mode == CW_KEYER_IAMBIC_B && after) {
		// the memory: the other paddle pressed during the element goes next, held or not
		dit = dit || k->mem_dit;
		dah = dah || k->mem_dah;
	}
	k->mem_dit = k->mem_dah = false;
	if (dit && dah) {
		if (k->mode == CW_KEYER_ULTIMATIC) {
			return k->last_dah ? 2 : 1;
		}
		return !after ? (k->last_dah ? 2 : 1) : (k->el_dah ? 1 : 2);	// alternate; a squeeze from rest: the last pressed
	}
	return dah ? 2 : dit ? 1 : 0;
}

bool cw_keyer_run(cw_keyer_t *k, uint32_t upto, uint32_t dot_ms, cw_hand_t *h)
{
	if (dot_ms == 0) {
		dot_ms = 1;
	}
	uint32_t steps = upto - k->at;
	if ((int32_t)steps <= 0) {
		return true;
	}
	if (steps > RUN_MAX_MS) {
		steps = RUN_MAX_MS;
	}
	bool bug = k->mode == CW_KEYER_BUG;
	for (uint32_t s = 0; s < steps; s++) {
		uint32_t t = ++k->at;
		paddles(k, t);
		bool on = false;
		if (bug) {
			// Dits from the dit paddle's press in steps of two dots; each dit and its space whole, the next only if
			// the paddle is still held. The dah paddle is the key itself
			if (!k->run && k->dit) {
				k->run = true;
				k->el_ms = t;
			}
			if (k->run) {
				uint32_t ph = (t - k->el_ms) % (2u * dot_ms);
				if (ph == 0 && t != k->el_ms && !k->dit) {
					k->run = false;
				} else {
					on = ph < dot_ms;
				}
			}
			on = on || k->dah;
		} else {
			// An element (a dit, or a dah of three dots), then a dot of space; at its end the paddles choose the next
			uint32_t len = k->el_dah ? 3u * dot_ms : dot_ms;
			if (k->run && t - k->el_ms >= len + dot_ms) {
				k->run = false;
				uint8_t e = next(k, true);
				if (e != 0) {
					k->run = true;
					k->el_dah = e == 2;
					k->el_ms = t;
				}
			} else if (!k->run) {
				uint8_t e = next(k, false);
				if (e != 0) {
					k->run = true;
					k->el_dah = e == 2;
					k->el_ms = t;
				}
			}
			len = k->el_dah ? 3u * dot_ms : dot_ms;
			on = k->run && t - k->el_ms < len;
		}
		if (on != k->key) {
			k->key = on;
			if (!cw_hand_edge(h, on, t)) {
				return false;
			}
		}
	}
	return true;
}

bool cw_keyer_idle(const cw_keyer_t *k)
{
	return !k->dit && !k->dah && !k->run && k->n == 0 && !k->mem_dit && !k->mem_dah;
}
