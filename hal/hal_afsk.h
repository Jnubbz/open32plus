// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Audio frequency shift keying for APRS (docs/spec/aprs.md 5): the board sends a prepared tone sequence through its
// FM transmitter, and, where it can sample received FM audio, hands that audio to the core's demodulator.
//
// Transmit: the core keys the transmitter itself (board.radio->tx() in FM on the APRS frequency, no subtone), then
// calls tx_start with the tones: one bit per symbol, LSB first in each byte, 1 = mark. The board switches the tone
// at each symbol boundary (a timer at the baud rate) with phase continuity where the hardware allows, and keeps its
// own bus traffic for that alone until tx_busy() goes false; the radio core does not touch the transmitter
// meanwhile. tx_stop puts the microphone path back (call it after the end, or to cut a transmission short).
//
// Receive (optional, rx_start NULL when the board has no way to sample received audio): the FM audio after the
// discriminator (de-emphasized as the speaker gets it is fine), as signed 16-bit PCM at rx_rate_hz; the core reads it
// from the radio task with rx_read every poll, so the board buffers at least 100 ms.
#ifndef HAL_AFSK_H
#define HAL_AFSK_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	// Transmit (NULL tx_start, tx_busy and tx_stop when the board can only receive)
	// Start sending nbits symbols of tones at baud, mark_hz for a 1 and space_hz for a 0. level 0-100 sets the
	// deviation (50 = the board's nominal, about 3 kHz peak at 25 kHz bandwidth). tones stays valid until tx_busy()
	// is false. False when it can't (the transmitter not keyed in FM, already sending)
	bool (*tx_start)(const uint8_t *tones, uint16_t nbits, uint16_t baud, uint16_t mark_hz, uint16_t space_hz,
			 uint8_t level);
	// Still sending
	bool (*tx_busy)(void);
	// Stop (early or after the end) and give the transmitter its microphone back; the core unkeys after this
	void (*tx_stop)(void);
	// Optional (NULL: none): a keyed tone through the same path, for CW messages (docs/spec/morse.md 5.2). Starts
	// silent; tone_key(true) sounds hz, false silences it, each edge ramped so the keying makes no clicks. level as
	// tx_start's. The channel's subtone stays on, as it does for the microphone. tx_stop ends it. False when it can't
	bool (*tone_start)(uint16_t hz, uint8_t level);
	void (*tone_key)(bool down);
	// Optional (NULL: tone_key at the time it is called): the key went down or up at ms on hal_millis(), now_ms now;
	// it plays lat_ms after ms, so an element keeps its length to the sample (afsk_tone_key_at, morse.md 8.2). False
	// when its queue is full
	bool (*tone_key_at)(bool down, uint32_t ms, uint32_t now_ms, uint32_t lat_ms);
	// Optional (NULL: none): the radio's own speaker plays the keyed tone while it is on the air, a sidetone
	// (morse.md 5.2). Only after tone_start; tx_stop turns it off
	void (*tone_side)(bool on);

	// Optional receive path (NULL / 0 when there is none)
	uint32_t rx_rate_hz;
	bool (*rx_start)(void);
	// Up to max samples received since the last call; 0 when none
	uint16_t (*rx_read)(int16_t *buf, uint16_t max);
	void (*rx_stop)(void);
} hal_afsk_t;

#endif
