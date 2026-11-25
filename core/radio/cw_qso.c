// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The CW QSO helper's text work (cw_qso.h, docs/spec/morse.md 9)
#include <string.h>
#include "radio/cw_qso.h"

#define WORK_MAX	16u	// distinct calls looked at in one text
#define WILD		'*'

static bool letter(char c) { return (c >= 'A' && c <= 'Z') || c == WILD; }
static bool digit(char c) { return (c >= '0' && c <= '9') || c == WILD; }

// A base call at w (n characters): prefix of 1 or 2, a digit, a suffix of 1 to 4 letters
static bool base(const char *w, size_t n)
{
	for (size_t p = 1; p <= 2 && p + 2 <= n; p++) {
		// the prefix: letters and digits, with a letter (W, KN, VE, 2E, 9A)
		bool ok = true, has_letter = false;
		for (size_t i = 0; i < p; i++) {
			ok = ok && (letter(w[i]) || digit(w[i]));
			has_letter = has_letter || letter(w[i]);
		}
		if (!ok || !has_letter || !digit(w[p])) {
			continue;
		}
		size_t s = n - p - 1;
		if (s < 1 || s > 4) {
			continue;
		}
		for (size_t i = p + 1; i < n; i++) {
			ok = ok && letter(w[i]);
		}
		if (ok) {
			return true;
		}
	}
	return false;
}

bool cw_qso_is_call(const char *w, size_t n, bool *doubtful)
{
	if (n < 3 || n > CW_QSO_CALL_MAX) {
		return false;
	}
	size_t wild = 0, bases = 0;
	for (size_t i = 0; i < n; i++) {
		char c = w[i];
		if (c == WILD) {
			wild++;
		} else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/')) {
			return false;
		}
	}
	if (wild > 2 || wild * 2 >= n) {
		return false;	// mostly guesses: not a call to offer
	}
	// '/' parts: one is the base call, the others 1 to 4 characters
	size_t start = 0;
	for (size_t i = 0; i <= n; i++) {
		if (i < n && w[i] != '/') {
			continue;
		}
		size_t len = i - start;
		if (len == 0) {
			return false;
		}
		if (base(w + start, len)) {
			bases++;
		} else if (len > 4) {
			return false;
		}
		start = i + 1;
	}
	if (bases == 0) {
		return false;
	}
	*doubtful = wild != 0;
	return true;
}

// a fits b: the same length, every character the same or a '*' in a
static bool fits(const char *a, const char *b)
{
	if (strlen(a) != strlen(b)) {
		return false;
	}
	for (; *a; a++, b++) {
		if (*a != *b && *a != WILD) {
			return false;
		}
	}
	return true;
}

uint8_t cw_qso_calls(const char *text, const char *own, cw_qso_call_t out[CW_QSO_CALLS])
{
	cw_qso_call_t work[WORK_MAX];
	uint8_t n = 0;
	// Newest first: the words from the end of the text
	size_t end = strlen(text);
	while (end > 0) {
		while (end > 0 && text[end - 1] == ' ') {
			end--;
		}
		size_t start = end;
		while (start > 0 && text[start - 1] != ' ') {
			start--;
		}
		size_t len = end - start;
		bool doubtful;
		if (len != 0 && cw_qso_is_call(text + start, len, &doubtful)) {
			char w[CW_QSO_CALL_MAX + 1];
			memcpy(w, text + start, len);
			w[len] = 0;
			if (own == NULL || !fits(w, own)) {	// our own call, even a doubtful copy of it, isn't offered
				uint8_t i = 0;
				while (i < n && strcmp(work[i].call, w) != 0) {
					i++;
				}
				if (i < n) {
					work[i].heard++;
				} else if (n < WORK_MAX) {
					memcpy(work[n].call, w, len + 1);
					work[n].heard = 1;
					work[n].doubtful = doubtful;
					n++;
				}
			}
		}
		end = start;
	}
	// A doubtful copy that fits a clean call is that call: counted there, not offered
	for (uint8_t i = 0; i < n; i++) {
		if (!work[i].doubtful) {
			continue;
		}
		for (uint8_t j = 0; j < n; j++) {
			if (!work[j].doubtful && fits(work[i].call, work[j].call)) {
				work[j].heard = (uint8_t)(work[j].heard + work[i].heard);
				work[i].heard = 0;
				break;
			}
		}
	}
	// Heard more than once, then once, then doubtful; newest first in each (work's order)
	uint8_t k = 0;
	for (uint8_t pass = 0; pass < 3 && k < CW_QSO_CALLS; pass++) {
		for (uint8_t i = 0; i < n && k < CW_QSO_CALLS; i++) {
			const cw_qso_call_t *c = &work[i];
			bool take = c->heard != 0 &&
				    (pass == 0 ? !c->doubtful && c->heard > 1 : pass == 1 ? !c->doubtful && c->heard == 1 : c->doubtful);
			if (take) {
				out[k++] = *c;
			}
		}
	}
	return k;
}

size_t cw_qso_fill(const char *tmpl, const char *them, char *out, size_t cap)
{
	static const char key[] = "{THEM}";
	size_t n = 0, tl = strlen(them);
	while (*tmpl) {
		if (strncmp(tmpl, key, sizeof key - 1) == 0) {
			if (n + tl >= cap) {
				return 0;
			}
			memcpy(out + n, them, tl);
			n += tl;
			tmpl += sizeof key - 1;
			continue;
		}
		if (n + 1 >= cap) {
			return 0;
		}
		out[n++] = *tmpl++;
	}
	if (cap != 0) {
		out[n] = 0;
	}
	return cap != 0 ? n : 0;
}
