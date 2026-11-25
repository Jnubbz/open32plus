// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// A straight key's keying (docs/spec/morse.md 8): the side key's presses and releases, time-stamped where they
// happened, played on the air lead_ms later. The receivers' squelch opens on the carrier during the lead-in, and the
// keying keeps the operator's own timing whatever the delay between the tasks. The carrier hangs hang_ms past the
// last release played. No RTOS: the host tests run it.
#ifndef CW_HAND_H
#define CW_HAND_H

#include <stdint.h>
#include <stdbool.h>

#define CW_HAND_EDGES	32u	// queued edges: 16 elements inside a lead-in, more than anyone keys

typedef struct {
	uint32_t t[CW_HAND_EDGES];	// when each queued edge happened
	uint32_t down;			// bit i: edge i is a press
	uint8_t head, n;		// the oldest queued edge, how many
	bool held;			// the side key's last edge was a press
	bool key;			// the key as played on the air
	uint32_t up_ms;			// when the air key last went up (the session's start before any)
} cw_hand_t;

// A session starts (the carrier keyed at now_ms)
void cw_hand_start(cw_hand_t *h, uint32_t now_ms);
// The side key went down or up at ms. A repeat of the last edge is dropped. False when the queue is full: the edge is
// lost, the caller ends the session
bool cw_hand_edge(cw_hand_t *h, bool down, uint32_t ms);
// The key on the air at now_ms: the edges older than lead_ms played. *done once the air key has been up hang_ms with
// nothing queued and the side key up
bool cw_hand_poll(cw_hand_t *h, uint32_t now_ms, uint32_t lead_ms, uint32_t hang_ms, bool *done);
// The oldest edge due at now_ms (older than lead_ms), taken off the queue: *down and when it plays (its time plus
// lead_ms) in *at_ms, for a tone keyed at a time (hal_afsk_t.tone_key_at). False when none is due. cw_hand_poll after
// it plays nothing more and reports done
bool cw_hand_next(cw_hand_t *h, uint32_t now_ms, uint32_t lead_ms, bool *down, uint32_t *at_ms);

#endif
