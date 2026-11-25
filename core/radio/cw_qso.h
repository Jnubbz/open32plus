// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The CW QSO helper's text work (docs/spec/morse.md 9): the callsigns in decoded CW, most trusted and newest first,
// and an exchange line with the other station's call filled in. No RTOS: the host tests run it.
#ifndef CW_QSO_H
#define CW_QSO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CW_QSO_CALLS		5u	// the most offered
#define CW_QSO_CALL_MAX		12u	// characters in one (KH6/W1AW/QRP is 12)

typedef struct {
	char call[CW_QSO_CALL_MAX + 1];
	uint8_t heard;		// times in the text (a doubtful copy that fits it counts)
	bool doubtful;		// holds a '*': a character the decoder couldn't read
} cw_qso_call_t;

// Whether the n characters at w are shaped like a callsign: a prefix (one or two characters with a letter, as W, KN,
// 2E, 9A), a digit, a suffix of one to four letters, and '/' parts of one to four (KH6/, /P, /QRP). '*' stands for any
// character and sets *doubtful
bool cw_qso_is_call(const char *w, size_t n, bool *doubtful);
// The callsigns in text (decoded CW, words between spaces): each once, with how often it was heard. Calls heard more
// than once first, then once, newest first in each; doubtful ones after the clean ones they don't fit. A doubtful copy
// that fits a clean call counts for it. own (the radio's callsign, "" none), or a doubtful copy of it, is left out. Returns how many, at most
// CW_QSO_CALLS
uint8_t cw_qso_calls(const char *text, const char *own, cw_qso_call_t out[CW_QSO_CALLS]);
// tmpl with each {THEM} replaced by them, into out (cap bytes with the terminator). Its length; 0 when it doesn't fit
size_t cw_qso_fill(const char *tmpl, const char *them, char *out, size_t cap);

#endif
