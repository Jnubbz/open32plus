// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A straight key's delayed keying (cw_hand.h, docs/spec/morse.md 8)
#include <string.h>
#include "radio/cw_hand.h"

void cw_hand_start(cw_hand_t *h, uint32_t now_ms)
{
	memset(h, 0, sizeof *h);
	h->up_ms = now_ms;
}

bool cw_hand_edge(cw_hand_t *h, bool down, uint32_t ms)
{
	if (down == h->held) {
		return true;	// a repeat: the key is already where it says
	}
	if (h->n == CW_HAND_EDGES) {
		return false;
	}
	uint8_t i = (uint8_t)((h->head + h->n) % CW_HAND_EDGES);
	h->t[i] = ms;
	if (down) {
		h->down |= 1u << i;
	} else {
		h->down &= ~(1u << i);
	}
	h->n++;
	h->held = down;
	return true;
}

bool cw_hand_poll(cw_hand_t *h, uint32_t now_ms, uint32_t lead_ms, uint32_t hang_ms, bool *done)
{
	// Each edge plays at its own time plus the lead-in: the air keying is the side key's, moved later
	while (h->n != 0 && (int32_t)(now_ms - (h->t[h->head] + lead_ms)) >= 0) {
		bool down = (h->down >> h->head) & 1u;
		if (!down && h->key) {
			h->up_ms = h->t[h->head] + lead_ms;
		}
		h->key = down;
		h->head = (uint8_t)((h->head + 1u) % CW_HAND_EDGES);
		h->n--;
	}
	*done = !h->key && h->n == 0 && !h->held && (int32_t)(now_ms - h->up_ms) >= (int32_t)hang_ms;
	return h->key;
}
