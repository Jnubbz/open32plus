// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Josh Noble (KN0BLE)
// CW messages (CONFIG_CW_SEND, docs/spec/morse.md 5): the text the UI hands the radio task, the canned messages'
// {CALL}, and what became of the last message. No hardware and no RTOS here: the radio core (radio_sm.c) keys it.
#ifndef RADIO_CWMSG_H
#define RADIO_CWMSG_H

#include <stdint.h>
#include <stdbool.h>
#include "radio/morse.h"

// What became of a message. Refusals happen before any carrier
typedef enum {
	CWMSG_NONE = 0,		// nothing sent yet
	CWMSG_SENT,		// keyed to the end
	CWMSG_CUT,		// stopped while keying (PTT, Back, a channel change)
	CWMSG_NOT_FM,		// refused: the channel isn't an FM channel
	CWMSG_BUSY,		// refused: not idle (reception, scan, another transmission)
	CWMSG_NO_TX,		// refused: the radio may not transmit here or now (RX only, inhibit, battery, no tone path)
	CWMSG_TOO_LONG,		// refused: longer than the channel's time-out timer at the set speed
	CWMSG_EMPTY,		// refused: nothing in the text has a Morse code
} cwmsg_result_t;

typedef struct {
	bool active;		// handed over and not ended yet (queued or keying)
	uint8_t last;		// cwmsg_result_t of the last message that ended
	uint8_t serial;		// counts ended messages, so a watcher sees a new result
	uint16_t requests, sent, cut, refused;
	uint32_t last_ms;	// the last sent message's time on air, carrier up to carrier down
} radio_cwmsg_view_t;

// The text as keyed: upper case, the characters with a Morse code, '<' and '>' for prosigns, single spaces between
// words, no space at either end. Returns the length (0: nothing to key); out is NUL terminated (cap includes it)
uint8_t radio_cwmsg_clean(const char *in, char *out, uint8_t cap);

// A canned message with {CALL} replaced by call. Returns the length, 0 when the template needs a callsign and call is
// empty or the result doesn't fit cap (NUL included)
uint8_t radio_cwmsg_expand(const char *tmpl, const char *call, char *out, uint8_t cap);

// UI task: hand a message over (cleaned). False while another is active, or when nothing in it can be keyed
bool radio_cwmsg_put(const char *text);
// Radio task: the message handed over (0 when none); it stays active until radio_cwmsg_ended()
uint8_t radio_cwmsg_take(char *out, uint8_t cap);
// Radio task: the message ended or was refused; ms is the time on air of a sent one
void radio_cwmsg_ended(cwmsg_result_t r, uint32_t ms);
void radio_cwmsg_get(radio_cwmsg_view_t *v);
// Back to nothing handed over (start-up, tests)
void radio_cwmsg_reset(void);

#endif
