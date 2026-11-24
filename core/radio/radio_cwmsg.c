// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Josh Noble (KN0BLE)
// CW messages (radio_cwmsg.h, docs/spec/morse.md 5)
#include "config.h"
#if CONFIG_CW_SEND	// the whole file
#include <string.h>
#include "radio/radio_cwmsg.h"

// Handover: written by the UI task only while pending is false, read by the radio task while it is true. The radio
// task clears pending last (radio_cwmsg_ended), after it is done with the text
static char text[MORSE_TEXT_MAX];
static volatile uint8_t text_len;
static volatile bool pending;
static radio_cwmsg_view_t stats;

static char upper(char c)
{
	return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

uint8_t radio_cwmsg_clean(const char *in, char *out, uint8_t cap)
{
	uint8_t n = 0;
	bool space = false;
	if (cap == 0) {
		return 0;
	}
	for (; *in != '\0'; in++) {
		char c = upper(*in);
		if (c == ' ' || c == '\t' || c == '\n') {
			space = n != 0;		// a run of spaces is one; none before the first character
			continue;
		}
		if (morse_code(c) == 0 && c != '<' && c != '>') {
			continue;		// nothing to key
		}
		if ((space ? 2u : 1u) > (uint8_t)(cap - 1u - n)) {
			break;			// full: cut at a character, NUL kept
		}
		if (space) {
			out[n++] = ' ';
			space = false;
		}
		out[n++] = c;
	}
	out[n] = '\0';
	return n;
}

uint8_t radio_cwmsg_expand(const char *tmpl, const char *call, char *out, uint8_t cap)
{
	static const char key[] = "{CALL}";
	size_t call_len = call != NULL ? strlen(call) : 0;
	size_t n = 0;
	if (cap == 0) {
		return 0;
	}
	while (*tmpl != '\0') {
		if (strncmp(tmpl, key, sizeof(key) - 1u) == 0) {
			if (call_len == 0 || n + call_len >= cap) {
				out[0] = '\0';
				return 0;
			}
			memcpy(out + n, call, call_len);
			n += call_len;
			tmpl += sizeof(key) - 1u;
			continue;
		}
		if (n + 1u >= cap) {
			out[0] = '\0';
			return 0;
		}
		out[n++] = *tmpl++;
	}
	out[n] = '\0';
	return (uint8_t)n;
}

bool radio_cwmsg_put(const char *msg)
{
	if (pending) {
		return false;
	}
	uint8_t n = radio_cwmsg_clean(msg, text, sizeof(text));
	stats.requests++;
	if (n == 0) {
		stats.refused++;
		stats.last = CWMSG_EMPTY;
		stats.serial++;
		return false;
	}
	text_len = n;
	pending = true;
	return true;
}

uint8_t radio_cwmsg_take(char *out, uint8_t cap)
{
	uint8_t n = text_len;
	if (!pending || n == 0) {
		return 0;
	}
	if (n > cap) {
		n = cap;
	}
	memcpy(out, text, n);
	text_len = 0;
	return n;
}

void radio_cwmsg_ended(cwmsg_result_t r, uint32_t ms)
{
	if (r == CWMSG_SENT) {
		stats.sent++;
		stats.last_ms = ms;
	} else if (r == CWMSG_CUT) {
		stats.cut++;
	} else {
		stats.refused++;
	}
	stats.last = (uint8_t)r;
	stats.serial++;
	text_len = 0;
	pending = false;
}

void radio_cwmsg_get(radio_cwmsg_view_t *v)
{
	*v = stats;
	v->active = pending;
}

void radio_cwmsg_reset(void)
{
	text_len = 0;
	pending = false;
}

#endif	// CONFIG_CW_SEND
