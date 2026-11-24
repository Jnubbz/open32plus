// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// Morse from received FM audio (morse_decode.h, docs/spec/morse.md 7)
#include <string.h>
#include "radio/morse.h"
#include "radio/morse_decode.h"

// 2 cos(2 pi f / 8000) in Q14 for f = 350, 400 ... 1050 Hz
static const int32_t coef[MDEC_BINS] = {
	31538, 31164, 30743, 30274, 29758, 29197, 28590, 27939, 27246, 26510, 25733, 24917, 24062, 23170, 22243,
};

// A Hann window over the block, Q15: a tone between two bins stays in the few bins around it instead of leaking into
// all of them, so the bins far from it measure the noise
static const int16_t hann[MDEC_BLOCK] = {
	0, 51, 202, 453, 802, 1247, 1786, 2414, 3129, 3925,
	4799, 5743, 6754, 7823, 8946, 10114, 11321, 12559, 13821, 15098,
	16383, 17669, 18946, 20208, 21446, 22653, 23821, 24944, 26013, 27024,
	27968, 28842, 29638, 30353, 30981, 31520, 31965, 32314, 32565, 32716,
	32767, 32716, 32565, 32314, 31965, 31520, 30981, 30353, 29638, 28842,
	27968, 27024, 26013, 24944, 23821, 22653, 21446, 20208, 18946, 17669,
	16384, 15098, 13821, 12559, 11321, 10114, 8946, 7823, 6754, 5743,
	4799, 3925, 3129, 2414, 1786, 1247, 802, 453, 202, 51,
};

#define LOCK_RATIO	4u	// a new tone: this many times the other bins' mean, in level
#define LOCK_BLOCKS	3u	// ... for this many blocks in a row
#define NEAR_STEPS	4u	// bins this close to the strongest share its tone
#define RELOCK_MS	1000u	// once an over's first element was heard, the key up this long before the lock moves
#define SNR_MIN		3u	// key down needs the peak this many times the floor
#define DOT_MS_MIN	25u	// 48 wpm
#define DOT_MS_MAX	250u	// under 5 wpm
#define DOT_MS_START	60u	// 20 wpm until the first elements say otherwise
#define WORD_MS_MAX	3000u	// longer gaps teach nothing about the spacing

static const char punct[] = "/-=?.,+@'():\"!";

uint8_t morse_char(uint8_t code, char *out)
{
	for (char c = 'A'; c <= 'Z'; c++) {
		if (morse_code(c) == code) {
			out[0] = c;
			return 1;
		}
	}
	for (char c = '0'; c <= '9'; c++) {
		if (morse_code(c) == code) {
			out[0] = c;
			return 1;
		}
	}
	for (const char *p = punct; *p; p++) {
		if (morse_code(*p) == code) {
			out[0] = *p;
			return 1;
		}
	}
	if (code == 0x45) {	// ...-.- SK, end of contact
		memcpy(out, "<SK>", 4);
		return 4;
	}
	out[0] = '*';
	return 1;
}

void morse_dec_init(morse_dec_t *d)
{
	memset(d, 0, sizeof *d);
	d->dot_ms = DOT_MS_START;
	d->dash_ms = 3u * DOT_MS_START;
	d->gap_ms = 3u * DOT_MS_START;
}

uint16_t morse_dec_hz(const morse_dec_t *d)
{
	return d->locked ? (uint16_t)(MDEC_HZ_MIN + d->bin * MDEC_HZ_STEP) : 0;
}

uint8_t morse_dec_wpm(const morse_dec_t *d)
{
	return (uint8_t)(1200u / d->dot_ms);
}

static uint32_t isqrt64(uint64_t v)
{
	uint64_t r = 0, bit = (uint64_t)1 << 62;
	while (bit > v) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (v >= r + bit) {
			v -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)r;
}

typedef struct {
	char *p;
	size_t n, max;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
	for (size_t i = 0; i < n && o->n < o->max; i++) {
		o->p[o->n++] = s[i];
	}
}

// The lower and upper group of a history whose longest is at least ratio/2 times its shortest (two kinds of length
// in it): their means. False when the lengths are all alike
static bool split(const uint16_t *h, uint8_t n, uint32_t ratio_x2, uint16_t *lo, uint16_t *hi)
{
	if (n > MDEC_HIST) {
		n = MDEC_HIST;
	}
	if (n < 2) {
		return false;
	}
	uint32_t mn = h[0], mx = h[0];
	for (uint8_t i = 1; i < n; i++) {
		mn = h[i] < mn ? h[i] : mn;
		mx = h[i] > mx ? h[i] : mx;
	}
	if (mx * 2u < mn * ratio_x2) {
		return false;
	}
	uint32_t mid = (mn + mx) / 2u, ls = 0, ln = 0, us = 0, un = 0;
	for (uint8_t i = 0; i < n; i++) {
		if (h[i] < mid) {
			ls += h[i];
			ln++;
		} else {
			us += h[i];
			un++;
		}
	}
	*lo = (uint16_t)(ls / ln);
	*hi = (uint16_t)(us / un);
	return true;
}

static void push(uint16_t *h, uint8_t *n, uint32_t ms)
{
	h[*n % MDEC_HIST] = (uint16_t)(ms > 0xFFFFu ? 0xFFFFu : ms);
	if (*n < 2u * MDEC_HIST) {
		(*n)++;
	} else {
		*n = MDEC_HIST;
	}
}

// The character heard so far, out: its marks classed by the lengths learned now, so a character heard while the
// speed was still being learned comes out right once its own dots and dashes told them apart
static void end_char(morse_dec_t *d, out_t *o)
{
	if (d->el_n == 0) {
		return;
	}
	uint8_t code = 0xFF;	// too many elements: '*'
	if (d->el_n <= 7u) {
		uint32_t thr = ((uint32_t)d->dot_ms + d->dash_ms) / 2u;
		code = 1;
		for (uint8_t i = 0; i < d->el_n; i++) {
			code = (uint8_t)((code << 1) | (d->el[i] > thr ? 1u : 0u));
		}
	}
	char t[4];
	put(o, t, morse_char(code, t));
	d->el_n = 0;
	d->char_out = true;
	d->word_chars++;
	d->chars++;
}

// A mark of ms ended. Two kinds of length among the last marks are dots and dashes; while they are all alike (EEE,
// TTT, or the first few), the mark is classed against the lengths learned and pulls them over, kept 1:3
static void mark(morse_dec_t *d, uint32_t ms)
{
	push(d->marks, &d->mark_n, ms);
	uint16_t lo, hi;
	if (split(d->marks, d->mark_n, 4u, &lo, &hi)) {
		d->dot_ms = lo;
		d->dash_ms = hi;
	} else if (ms * 2u > (uint32_t)d->dot_ms + d->dash_ms) {
		d->dash_ms = (uint16_t)((d->dash_ms + ms) / 2u);
		d->dot_ms = (uint16_t)(d->dash_ms / 3u);
	} else {
		d->dot_ms = (uint16_t)((d->dot_ms + ms) / 2u);
		d->dash_ms = (uint16_t)(3u * d->dot_ms);
	}
	if (d->dot_ms < DOT_MS_MIN) {
		d->dot_ms = DOT_MS_MIN;
	} else if (d->dot_ms > DOT_MS_MAX) {
		d->dot_ms = DOT_MS_MAX;
	}
	if (d->el_n < MDEC_EL_MAX) {
		d->el[d->el_n] = (uint16_t)(ms > 0xFFFFu ? 0xFFFFu : ms);
	}
	if (d->el_n < 0xFFu) {
		d->el_n++;
	}
}

// Word gaps: halfway between the character gap and the word gap. Until both kinds were heard, 5 dots (standard
// spacing), or 5/3 of the character gap when that is longer (Farnsworth)
static uint32_t word_ms(const morse_dec_t *d)
{
	if (d->word_ms != 0) {
		return ((uint32_t)d->gap_ms + d->word_ms) / 2u;
	}
	uint32_t w = (uint32_t)d->gap_ms * 5u / 3u;
	return w > 5u * d->dot_ms ? w : 5u * d->dot_ms;
}

// A gap of ms ended as a mark began. Gaps that ended a character teach the spacing: two kinds of length among them are
// the character and word gaps. Farnsworth spacing stretches character gaps past 5 dots, so the first ones read as word
// gaps until a real word gap shows them shorter
static void gap(morse_dec_t *d, uint32_t ms)
{
	if (d->char_out && ms < WORD_MS_MAX) {
		push(d->gaps, &d->gap_n, ms);
		uint16_t lo, hi;
		if (split(d->gaps, d->gap_n, 3u, &lo, &hi)) {
			d->gap_ms = lo;
			d->word_ms = hi;
		} else if (!d->word_out) {
			d->gap_ms = (uint16_t)((d->gap_ms + ms) / 2u);
		}
	}
	if (d->gap_ms < 2u * d->dot_ms) {
		d->gap_ms = (uint16_t)(3u * d->dot_ms);
	}
	if (d->word_out) {
		d->word_chars = 0;
	}
	d->char_out = false;
	d->word_out = false;
}

// One 10 ms block's level on the locked tone
static void block(morse_dec_t *d, uint32_t m, out_t *o)
{
	// key up or down by the levels so far, then this block teaches the level of the state it is in
	uint32_t span = d->peak > d->floor ? d->peak - d->floor : 0;
	bool signal = d->peak >= SNR_MIN * d->floor && span != 0;
	bool on = signal && (d->on ? m > d->floor + span * 2u / 5u : m > d->floor + span * 3u / 5u);
	if (on) {
		d->peak += (uint32_t)(((int32_t)m - (int32_t)d->peak) / 4);
	} else {
		d->floor += (uint32_t)(((int32_t)m - (int32_t)d->floor) / 4);
		d->peak -= (d->peak - d->floor) >> 8;	// a station fading: the threshold follows down slowly
	}
	if (m > d->peak) {
		d->peak = m;
	}
	if (m < d->floor) {
		d->floor = m;
	}
	d->run++;
	if (on != d->on) {
		uint32_t ms = (uint32_t)d->run * MDEC_BLOCK_MS - MDEC_BLOCK_MS;	// the block before this one ended it
		if (d->on) {
			mark(d, ms);
		} else {
			gap(d, ms);
		}
		d->on = on;
		d->run = 1;
		return;
	}
	if (!d->on) {
		uint32_t ms = (uint32_t)d->run * MDEC_BLOCK_MS;
		if (d->el_n != 0 && !d->char_out && ms >= 2u * d->dot_ms) {
			end_char(d, o);
		}
		if (d->char_out && !d->word_out && d->word_chars != 0 && ms >= word_ms(d)) {
			put(o, " ", 1);
			d->word_out = true;
		}
		if (d->run > 60000u) {
			d->run = 60000u;
		}
	}
}

// A block's levels: lock onto the strongest bin when it stands out and the key is up, then decode on it. A tone covers
// the bins within 200 Hz of it (the window's main lobe, 4 steps), so it is measured against the bins farther away
static void levels(morse_dec_t *d, out_t *o)
{
	uint32_t mag[MDEC_BINS];
	uint8_t best = 0;
	for (uint8_t b = 0; b < MDEC_BINS; b++) {
		int64_t s1 = d->s1[b], s2 = d->s2[b];
		int64_t p = s1 * s1 + s2 * s2 - ((coef[b] * s1) >> 14) * s2;
		mag[b] = isqrt64(p > 0 ? (uint64_t)p : 0u);
		if (mag[b] > mag[best]) {
			best = b;
		}
		d->s1[b] = d->s2[b] = 0;
	}
	uint32_t far = 0, far_n = 0;
	for (uint8_t b = 0; b < MDEC_BINS; b++) {
		if (b + NEAR_STEPS < best || b > best + NEAR_STEPS) {
			far += mag[b];
			far_n++;
		}
	}
	far = far_n != 0 ? far / far_n : 0;
	// A tone stands above the bins on both sides of it. Receiver noise slopes (de-emphasis puts most of it low), so its
	// strongest bin is an end one: it never locks, even when it stands out from the far bins in the gaps
	bool peak = best > 0 && best + 1u < MDEC_BINS && mag[best] >= mag[best - 1u] && mag[best] >= mag[best + 1u];
	// Mid-over the lock holds through the gaps; it moves after a pause, or at once before the over's first element
	bool may_move = !d->locked || d->mark_n == 0 || (uint32_t)d->run * MDEC_BLOCK_MS >= RELOCK_MS;
	bool locking = false;
	if (!d->on && may_move && peak && (!d->locked || best != d->bin) && mag[best] > LOCK_RATIO * far &&
	    mag[best] != 0) {
		// a bin next to the lock is the same tone between two bins, not another one
		bool near = d->locked && best + 2u >= d->bin && best <= d->bin + 2u;
		if (!near) {
			d->cand_n = best == d->cand ? (uint8_t)(d->cand_n + 1u) : 1u;
			d->cand = best;
			if (d->cand_n >= LOCK_BLOCKS) {
				d->bin = best;
				d->locked = true;
				d->peak = mag[best];
				d->floor = mag[best] / 8u;	// the key-up level in this bin is learned at the first gap
				d->cand_n = 0;
				locking = true;
			}
		}
	} else {
		d->cand_n = 0;
	}
	if (d->locked) {
		block(d, mag[d->bin], o);
		if (locking && d->on) {
			d->run = LOCK_BLOCKS;	// the element began as the tone was first seen
		}
	}
}

size_t morse_dec_feed(morse_dec_t *d, const int16_t *s, size_t n, char *out, size_t max)
{
	out_t o = { out, 0, max };
	for (size_t i = 0; i < n; i++) {
		int32_t x = ((int32_t)s[i] * hann[d->n]) >> 15;
		for (uint8_t b = 0; b < MDEC_BINS; b++) {
			int32_t v = x + (int32_t)(((int64_t)coef[b] * d->s1[b]) >> 14) - d->s2[b];
			d->s2[b] = d->s1[b];
			d->s1[b] = v;
		}
		if (++d->n == MDEC_BLOCK) {
			d->n = 0;
			levels(d, &o);
		}
	}
	return o.n;
}

size_t morse_dec_flush(morse_dec_t *d, char *out, size_t max)
{
	out_t o = { out, 0, max };
	if (d->on) {
		mark(d, (uint32_t)d->run * MDEC_BLOCK_MS);
		d->on = false;
	}
	end_char(d, &o);
	if (d->word_chars != 0 && !d->word_out) {
		put(&o, " ", 1);
	}
	d->char_out = d->word_out = false;
	d->word_chars = 0;
	// the next over may be another station: its speed and spacing are learned again, from these as a first guess
	d->mark_n = d->gap_n = 0;
	d->word_ms = 0;
	d->run = 0;
	memset(d->s1, 0, sizeof d->s1);
	memset(d->s2, 0, sizeof d->s2);
	d->n = 0;
	return o.n;
}
