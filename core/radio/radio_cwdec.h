// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The CW decoder (CONFIG_CW_DECODE, docs/spec/morse.md 7): while the UI's Decode CW screen is open, the radio task
// takes the board's received FM audio (hal_afsk_t rx path, 8 kHz) and decodes Morse from it while the squelch is open.
// The text goes into a buffer the UI reads. The UI calls want / clear / get; the radio core calls service / release.
#ifndef RADIO_CWDEC_H
#define RADIO_CWDEC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CWDEC_TEXT_MAX	160u	// the decoded text kept (the newest), with its terminator

typedef enum {
	CWDEC_OFF,		// not wanted
	CWDEC_LISTENING,	// the receive audio is on, the squelch closed
	CWDEC_HEARING,		// the squelch is open: decoding
	CWDEC_NOT_FM,		// the channel isn't FM
	CWDEC_BUSY,		// transmitting, or APRS listens on this frequency with the same audio
	CWDEC_NO_PATH,		// the board can't sample received audio
} cwdec_state_t;

typedef struct {
	uint8_t state;		// cwdec_state_t
	uint16_t hz;		// the tone locked on, 0 none yet
	uint8_t wpm;		// the speed learned
	uint32_t chars;		// characters decoded since the screen opened
	uint32_t serial;	// moves on with every change of the text
} radio_cwdec_view_t;

// UI task: the screen opened (true) or closed. Opening starts the text afresh
void radio_cwdec_want(bool on);
// UI task: empty the text
void radio_cwdec_clear(void);
// UI task: the state, and the text (the newest part that fits max, terminated)
void radio_cwdec_get(radio_cwdec_view_t *v, char *text, size_t max);

// What the radio core says about the channel, every poll
typedef struct {
	bool fm;		// the channel is FM and no hotspot runs
	bool tx;		// the radio transmits
	bool rx_open;		// the squelch is open
	bool busy;		// another user has the receive audio (APRS on this frequency)
} radio_cwdec_in_t;

// Radio task, every poll: starts and stops the audio, decodes. True when the view or the text changed
bool radio_cwdec_service(const radio_cwdec_in_t *in, uint32_t now_ms);
// Radio task: let go of the audio now (before tuning away: a P25, NXDN or YSF engine can't start while it is held)
void radio_cwdec_release(void);
// The decoder holds the receive audio (the radio core keeps the receiver awake meanwhile)
bool radio_cwdec_running(void);

#endif
