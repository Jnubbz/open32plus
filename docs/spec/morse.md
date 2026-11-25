<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Josh Noble (KN0BLE) -->

# Morse timing

`core/radio/morse.c` turns text into key-down and key-up segments. It has no hardware: the radio core keys the board's
TX tone with it (`hal_radio_t` `tx_tone`, `tx_tone_key`). Users today: the hotspot's CW ID
([mmdvm-protocol.md](mmdvm-protocol.md) 2.11) and the single frequency repeater's CW ID ([sfr.md](sfr.md)).

## 1. Timing

PARIS standard: a word is 50 units, so one unit (a dot) is 1200 / wpm ms. Speeds are clamped to `MORSE_WPM_MIN` ..
`MORSE_WPM_MAX` (5 .. 40 wpm).

| Element | Standard length | With Farnsworth spacing |
|---|---|---|
| Dot | 1 unit | same |
| Dash | 3 units | same |
| Gap inside a character | 1 unit | same |
| Gap between characters | 3 units | 3 t_a / 19 |
| Gap between words | 7 units | 7 t_a / 19 |

Spaces in the text are word gaps; several in a row count as one. Leading spaces and characters without a code are
skipped. There is no gap after the last element.

## 2. Farnsworth spacing

`morse_start_farnsworth(m, text, len, wpm, eff_wpm, now)` keys characters at `wpm` and spaces them so the text as a
whole runs at `eff_wpm`. It follows the ARRL standard (J. Bloom, KE3Z, "A Standard for Morse Timing Using the
Farnsworth Technique", QST, April 1990). With c the character speed and s the overall speed, both in wpm:

t_a = (60 c - 37.2 s) / (s c) seconds

is the spacing a 50 unit word needs at s. It is shared 3:7 over the character and word gaps. `morse_gap_ms()` returns
both in ms, rounded once. `eff_wpm` 0, or not below `wpm`, gives standard spacing. `morse_start()` is
`morse_start_farnsworth()` with `eff_wpm` 0.

| c / s (wpm) | Character gap | Word gap | "PARIS " |
|---|---|---|---|
| 20 / 20 | 180 ms | 420 ms | 3000 ms |
| 20 / 10 | 654 ms | 1525 ms | 6001 ms |
| 18 / 5 | 1568 ms | 3660 ms | 12 s |

## 3. Characters

ITU-R M.1677-1: letters (either case), digits and `. , ? ' ! / ( ) : = + - " @`.

Prosigns: letters between `<` and `>` are keyed as one character, with a 1 unit gap between them. `<AR>` keys
`.-.-.`, the same as `+`. A space or `>` ends the prosign; an unclosed `<` runs to the end of the text. The brackets
themselves are not keyed.

| Prosign | Keyed | Same as |
|---|---|---|
| `<AR>` | `.-.-.` | `+` |
| `<BT>` | `-...-` | `=` |
| `<KN>` | `-.--.` | `(` |
| `<SK>` | `...-.-` | — |

## 4. Text length

`morse_t` holds a copy of the text, at most `MORSE_TEXT_MAX` characters: 144 with `CONFIG_CW_SEND`, else
`MORSE_ID_MAX` (40), the CW IDs' limit, which they keep either way. Longer text is cut. A build may define
`MORSE_TEXT_MAX` itself, up to 255.

## 5. Sending a message as CW (`CONFIG_CW_SEND`)

It follows the APRS transmission's model ([aprs.md](aprs.md) 6): a request from the UI, keyed by the radio core from
idle, then back to the channel. Not yet checked on a radio: test into a dummy load with an SDR watching first.

### 5.1 What the user does

| Step | Behavior |
|---|---|
| Open | Messages > Send CW (the list's last row) opens `cw-compose`: the canned CW messages, then Write... |
| Canned | `CQ CQ CQ DE {CALL} {CALL} K`, `QRZ? DE {CALL} K`, `TU 73 DE {CALL} <SK>`, `DE {CALL}`. On air text: never translated |
| Write | The message editor (`message-text`, up to 143 characters) in capitals only: the letter keys skip lower case and **#** goes between ABC and 123 (Morse has no case). OK there sends it as CW |
| Send | OK hands the text to the radio core (`radio_req_cw_send`) and opens `cw-send`: the text, "Sending CW...", then the outcome. It opens over the CW list, the editor closed: Back stops a message still keying and returns to the list, to send another |
| Settings | Settings > CW, between My radio and Signaling tones (no digit key, so the digit keys' groups keep theirs): Speed 5..40 wpm (`cw_wpm`, 0 = 20), Farnsworth Off or 5..18 wpm (`cw_eff_wpm`, CW messages only; the CW IDs keep standard spacing), Tone 400..1000 Hz (`cw_tone_hz`, 0 = 800), Lead-in 300..2000 ms in 100 ms steps (`cw_lead`, 0 = 600; CW messages only) |

`{CALL}` is the callsign in the radio name (`radio_cwid_callsign`, the SFR CW ID's rule). A canned message that needs it
is dimmed when the name holds none, and refused with a toast.

The text is cleaned before it is handed over (`radio_cwmsg_clean`): upper case, only characters with a code, `<` and
`>`, single spaces. Nothing left: refused at once (`empty`).

### 5.2 Where and how it keys

| Rule | Behavior | Why |
|---|---|---|
| Channel | Only an FM channel (`ch.mode`): a DMR, P25 or NXDN channel is refused (`not_fm`) | MCW is analog FM; keying it on a digital channel's frequency would put FM onto a digital repeater's input |
| Frequency, power | The channel's TX frequency, bandwidth and power | The user picked the channel |
| Subtone | The channel's TX CTCSS/DCS goes out with the tone | A tone-access repeater opens for it |
| Start | Only from idle (`cwmsg_idle`), the APRS transmission's rules: no PTT, no reception, no scan, no hotspot, no DMR call or data, no VOX, no roger beep or burst (`busy`) | Never steps on traffic or another keying |
| Guards | `tx_check()`, `tx_held()`, an RX-only channel or a board without `tx_tone` refuse it (`no_tx`), and `key_tx()` checks again | Every keying path shares them |
| Length | Refused when lead + `morse_ms()` + tail is not shorter than the channel's TOT (`too_long`) | The TOT is never the thing that ends a CW message. The TOT still runs as a backstop |
| Keying | `cw_begin()`, as the CW IDs: carrier up, the lead-in from Settings > CW (`cw_lead`, 300-2000 ms in 100 ms steps, default 600; an ID's is 150 ms), the Morse on the board's keyed audio tone (`hal_afsk_t.tone_start` / `tone_key`, 5 ms raised-cosine edges) where it has one, else on `tx_tone` / `tx_tone_key` as the IDs, 150 ms tail. The view: `RS_TX`, `tx_auto`, `cw_tx` | One keyer for every CW path. The receivers are handhelds: squelch, battery save and a subtone decoder open on the carrier, and with 150 ms the first letter was lost on the air. 600 ms still lost it to a receiver with battery save on: a setting, since the sender can't change the receiver. The audio path keeps the channel's subtone intact: on the HR_C7000 the tone generator's deviation is also the subtone's, and a tone squelch receiver stayed shut ([modem-hrc7000.md](modem-hrc7000.md) 8.3) |
| End | `tune()` back to the channel, as an APRS transmission ends (`sent`) | The channel exactly as before |
| Stop | `RREQ_CW_STOP` (Back), PTT, the monitor, hotspot mode, or any request that retunes or acts on the channel (`RQ_RETUNES`, `RQ_SELECTED`): key up and carrier off at once, then `tune()` (`cut`). PTT's press and release do nothing else: press it again to talk. While the message keys, the radio core sets `hal_input_t.ptt_stops_tx`, so a board whose PTT line trusts no new press during a transmission ([gotchas.md](gotchas.md) 16.2) trusts one | The user is always in control of the transmitter |

The handover (`radio_cwmsg.c`) holds one message: a second is refused while one is queued or keying. A request lost on
a full queue frees it again.

### 5.3 Size and switches

| Item | Cost |
|---|---|
| `CONFIG_CW_SEND` | `config/features.mk` and `core/config.h`, = `FM_TX_TONES` (it needs the TX tone path); [feature-matrix.md](../feature-matrix.md) has the flash figure |
| `RADIO_SM_CW` | Also on with `CONFIG_CW_SEND`. The keyer is outside `CONFIG_DMR`, so FM-only builds have it |
| RAM | About 300 bytes: `MORSE_TEXT_MAX` 144 in `radio_sm_t` and in the handover, and the text `cw-send` shows |
| Settings | `cw_eff_wpm` in the old `reserved10[1]` (page 1, offset 399), `cw_lead` in the old `reserved11[0]` (offset 406; [settings-store.md](settings-store.md)) |

### 5.4 Tests and debugging

| Test | Checks |
|---|---|
| `sharedPttTest` | A press during a transmission counts only while `ptt_stops_tx` is on |
| `aprsTest` | `afsk_tone`: silent while up, level and frequency while down, ramped edges |
| `radioCoreTest` | Keying "AB" (the 600 ms default lead, a longer one from the settings, element timing, tail, back to the channel), PTT cutting it without keying, `ptt_stops_tx` on only while it keys, stop, a channel change, every refusal, the channel's bandwidth and CTCSS, the audio tone path and the fallback without it, Farnsworth and tone from the settings, the UI services and settings |
| `morseTest` | `morse_ms()` against the keyed timelines, `radio_cwmsg_clean()` and `radio_cwmsg_expand()` |
| `uiFlowTest` | The Messages row, `cw-compose`, `cw-send`, Back (to the CW list, then Messages), Write..., Settings > CW |
| Debug link | View section 39 `cw` ([debuglink.md](debuglink.md)) |
| HIL | `tests/hil/features.toml` `cw_send`: opt-in, `--allow-rf` only, into a dummy load |

## 6. Tests

`tests/host/morseTest.c`: punctuation, prosigns, Farnsworth gaps and keyed timelines, and a 160 character buffer.
`tests/host/mmdvmTest.c`: PARIS at 20 wpm, the code table and keying over time for the CW ID. CW messages: 5.4.
The decoder: 7.5. The straight key: 8.3. The QSO helper: 9.3.

## 7. Decoding CW (`CONFIG_CW_DECODE`)

Messages > Decode CW shows Morse decoded from the FM channel: MCW, a keyed tone in FM audio, as CW messages send it.
It decodes nothing on HF: the radios receive FM.

### 7.1 What the user sees

| Item | Behavior |
|---|---|
| Messages | "Decode CW", the last row, after Send CW |
| `cw-decode` | The newest text above a status row: Listening, Hearing (with the tone and speed learned, "800 Hz 18 wpm"), Needs an FM channel, Paused: transmitting or APRS, This radio can't decode. Older text scrolls off the top a word at a time |
| Keys | **#** clears the text. **Back** closes the screen, and the decoder stops |
| Text | As received, never translated. An unknown character is `*`; SK is `<SK>`; AR, BT and KN read as `+`, `=` and `(` |

### 7.2 Where the audio comes from

| Rule | Behavior | Why |
|---|---|---|
| Path | The board's received FM audio, 8 kHz (`hal_afsk_t` `rx_start` / `rx_read` / `rx_stop`, APRS receive's path). A board without it, or at another rate: This radio can't decode | One path for both |
| When | While the screen is open and the channel is FM, not while transmitting or in hotspot mode | Nothing runs, and no battery goes, unless asked |
| APRS | On the APRS receive frequency APRS keeps the audio (Paused) | It was there first |
| Squelch | Only audio with the squelch open is decoded; as it closes, a character in progress and a word space come out (`morse_dec_flush`) | Noise between overs is not Morse |
| Retune | Off FM, or onto the APRS frequency, the path is let go before the board changes mode (`radio_cwdec_release`) | On the HR_C7000 a P25, NXDN or YSF engine can't start while the audio is held |
| Battery save | Off while the decoder runs | A sleeping receiver loses the first letters |
| Text | The newest 159 characters in `radio_cwdec.c`, written by the radio task, read by the UI under a sequence count | No lock between the tasks |

### 7.3 The decoder (`core/radio/morse_decode.c`)

| Stage | How | Why |
|---|---|---|
| Blocks | 80 samples (10 ms) under a Hann window, 15 Goertzel bins from 350 to 1050 Hz, 50 Hz apart (100 Hz wide). The two end bins only guard: a tone locks from 400 to 1000 Hz | The window keeps a tone between two bins in the bins around it, so bins more than 4 steps away measure the noise |
| Tone | The strongest bin locks when it is at least as strong as both its neighbors and 4 times the far bins' mean, for 3 blocks with the key up; a bin within 2 steps of the lock is the same tone. Before an over's first element the lock moves freely; after it, only once the key has been up 1 s | Receiver noise slopes down from the low end after de-emphasis, so its strongest bin is an end bin and never locks. Holding the lock through the gaps keeps noise from moving it, which cut every element short by the 3 blocks a new lock takes. A second station takes over between overs |
| Key | Down above 3/5 of the way from the key-up level to the key-down level, up below 2/5, and only with the key-down level at least 3 times the key-up one. Each block's level teaches the state it was decided in | Follows a fading signal without a fixed threshold |
| Speed | The last 8 marks: when the longest is twice the shortest they split into dots and dashes; while they are all alike, a mark is classed against the lengths learned and the two kept 1:3. 25 to 250 ms dots (48 to under 5 wpm), 20 wpm to start | Learns a speed far from the start in a few elements |
| Characters | A gap over 2 dots ends one; its marks are classed when it ends, by the lengths learned then | The first character comes out right once its own dots and dashes told them apart |
| Words | The last 8 gaps that ended a character: when the longest is 1.5 times the shortest they split into character and word gaps, and the word threshold is halfway. Until then 5 dots, or 5/3 of the character gap when longer | Farnsworth spacing: only the first word's spacing is wrong |
| Overs | `morse_dec_flush` (the squelch closed) forgets the marks and gaps; the lengths stay as the first guess | Another station, another speed |

Measured on the host (`morseTest`, synthetic MCW with 5 ms raised-cosine edges): clean copy from 5 to 35 wpm and at
725 Hz, between bins; text intact with the tone 1 dB over white noise in the 4 kHz band; with Farnsworth 20/10 the
first word misspaced, the rest whole.

### 7.4 Size and switches

| Item | Cost |
|---|---|
| `CONFIG_CW_DECODE` | `config/features.mk` and `core/config.h`, = `APRS_RX` (its board path); [feature-matrix.md](../feature-matrix.md) has the flash figure |
| RAM | About 880 bytes: the decoder state, the text in the radio core, a block of samples, and copies of the text for the screen and the debug link |
| CPU | 13 Goertzel bins per sample at 8 kHz, about 100,000 multiplies a second, only while the screen is open |

### 7.5 Tests and debugging

| Test | Checks |
|---|---|
| `morseTest` | Every character's code back to its character; decoding at 5, 20 and 35 wpm, between bins, with Farnsworth, in noise, a second station after the squelch closed; nothing from noise alone |
| `aprsRadioTest` | Off until wanted, yields to APRS on its frequency, starts on another FM channel, decodes only with the squelch open and flushes as it closes, no battery saving meanwhile, # clears, closing stops the audio |
| `uiFlowTest` | The Messages row, `cw-decode` with the status and text, #, Back |
| Debug link | `cw.dec_*` in view section 39 ([debuglink.md](debuglink.md)) |
| HIL | `tests/hil/features.toml` `cw_decode` |

## 8. A straight key (`CONFIG_CW_SEND`)

A side key set to Straight key keys MCW on the FM channel by hand: the tone sounds on the air while the key is down.
It is a CW message whose keying comes from the key instead of text, so it shares section 5.2's rules: the channel,
power and subtone, the idle and transmit checks, the audio tone path, PTT and retunes cutting it.

### 8.1 What the user does

| Step | Behavior |
|---|---|
| Assign | Settings > Keys: a side key's press set to Straight key (`KEYFN_CW_KEY`, 22). Press only: the key sends raw down and up edges (`ACT_CW_KEY`), as Tone burst does, so it has no long press. Not on a home key (no raw edges there) |
| Key | The first press keys the channel; each press and release after it goes on the air a lead-in later, with the operator's own timing |
| Stop | The carrier drops once the key has been up for the hang time on the air. PTT, a retune or the monitor cut it at once |
| Settings | Settings > CW: Lead-in (`cw_lead`, shared with CW messages) is the delay; Key hang 300..3000 ms in 100 ms steps (`cw_hang`, 0 = 1000) is the hang. Tone (`cw_tone_hz`) is the tone; Speed and Farnsworth play no part |
| Refused | A press on a channel that isn't FM, while the radio isn't idle, or where a transmission isn't allowed beeps (`BEEP_ERROR`) and keys nothing. A press while a CW message keys does nothing |

### 8.2 The keying (`core/radio/cw_hand.c`)

| Rule | Behavior | Why |
|---|---|---|
| Time stamps | `radio_req_cw_key()` posts each edge with `hal_millis()` taken in the input task (`RREQ_CW_KEY`, arg2) | The keying plays from when the edge happened. The queue and the radio task's own timing add no jitter |
| Delay | An edge plays at its time plus the lead-in (`cw_hand_poll`, every `RADIO_TICK_MS`) | The receivers' squelch, battery save and subtone decoder open on the carrier before the first element, as for a message, and the first element keeps its length |
| Queue | 32 edges. A repeat of the last edge is dropped. A full queue ends the session (`cut`) | 16 elements inside a 2 s lead-in is past anyone's hand speed |
| Hang | Done once the air key has been up `cw_hang` with nothing queued and the side key up; then `cw_finish()` and `tune()` back to the channel | Semi break-in: the carrier stays up through the gaps between letters and words |
| Time-out | No length to check first: the channel's TOT ends a session as it ends PTT, with `BEEP_ERROR` | A held key can't transmit for ever |
| View | `RS_TX`, `cw_tx`, `tx_auto` false (the user keyed it: the screen wakes) | |
| Outcome | None in `radio_cwmsg_get()`: no message was handed over | The CW message screens show only messages |

The input scan (`INPUT_SCAN_MS`, 10 ms, two agreeing scans to debounce) sets the timing's resolution: both edges are
late by the same debounce, so an element's length is kept to within a scan.

The radio's own speaker plays no sidetone yet. The HR_C7000's sidetone (`hal_radio_t.tx_tone_side`) follows its tone
generator, which garbles the subtone ([modem-hrc7000.md](modem-hrc7000.md) 8.3). Whether the audio tone path can reach
the speaker during a transmission is not yet known. Listen on a second receiver meanwhile.

### 8.3 Size, switches and tests

| Item | Detail |
|---|---|
| Switch | Part of `CONFIG_CW_SEND` |
| RAM | About 140 bytes in `radio_sm_t` (`cw_hand_t`) |
| Settings | `cw_hang` in the old `reserved11`'s last byte (offset 407; [settings-store.md](settings-store.md)) |
| `morseTest` | `cw_hand`: the delay, element lengths kept, the hang, a press during the hang, a held key, a full queue, the clock wrapping |
| `keyassignTest` | Straight key's number, press only, raw edges through the gesture engine, the PTT lock, never on a home key |
| `radioCoreTest` | A dot and a dash keyed a lead-in late with their lengths, the hang, a late request playing on time, a press during the hang, the hang setting, PTT and a channel change cutting it, a CW message refused over it and the key ignored during one, refusals on DMR and TX inhibit, the time-out |
| Debug link | `cw.hang` and `cw.tx` in view section 39 ([debuglink.md](debuglink.md)) |

## 9. The QSO helper (`CONFIG_CW_QSO`)

On the decoder's screen, OK replies to a station heard: the callsigns in the decoded text, then exchange lines with
the one picked filled in, each sent as a CW message (section 5). It needs `CONFIG_CW_SEND` and `CONFIG_CW_DECODE`.

### 9.1 What the user does

| Step | Behavior |
|---|---|
| Reply | Decode CW > OK opens `qso-calls`, "Reply to": up to five callsigns from the text as it is then, and Enter call. A call heard more than once shows the count ("3x"). The decoder keeps running under it |
| Pick | A clean call opens `qso`, the exchange, in the list's place: Back from it returns to Decode CW, where OK makes a fresh list. The stack stays within its six levels with `cw-send` on top |
| Doubtful | A call with a `*` (a character the decoder couldn't read) is marked and opens the callsign editor (`qso-call`, capitals only) to finish it. So does Enter call. Something that isn't a callsign is refused ("Not a callsign") |
| Send | OK on a line sends it as a CW message and opens `cw-send` over the exchange: Back returns to the exchange for the next line. Lines are sent only from the exchange, so a press always stands between a misread call and the air |

The exchange lines are fixed for now, on air text that is never translated. `{THEM}` is the call picked, `{CALL}` the
radio's own callsign as in CW messages (a line is dimmed and refused without one):

| Line | Use |
|---|---|
| `{THEM} DE {CALL} {CALL} K` | Answering a CQ, or calling |
| `{THEM} DE {CALL} UR 599 599 BK` | The report |
| `{THEM} AGN? K` | Asking for a repeat |
| `{THEM} TU 73 DE {CALL} EE` | Signing |

### 9.2 Finding the calls (`core/radio/cw_qso.c`)

| Rule | Behavior | Why |
|---|---|---|
| Shape | A word is a call when one `/` part is a prefix (one or two characters with a letter: W, KN, 2E, 9A), a digit and a suffix of one to four letters, and the other parts are one to four characters (KH6/, /P, /QRP). At most 12 characters | Takes W1AW, KN0BLE, 2E0ABC, KH6/W1AW; leaves the CW around them: 599, 5NN, 73, TU, UR5, QRZ? |
| Doubtful | `*` stands for any character, at most two and under half the word. A doubtful copy that fits a clean call counts as that call | The decoder prints `*` for a character it couldn't read |
| Own call | The radio's callsign (`{CALL}`), or a doubtful copy of it, is left out | Our own CQ is in the text too |
| Order | Calls heard more than once, then once, then doubtful ones; newest first in each. Five at most | The station answering usually sends its call twice and last |

`cw_qso_fill()` puts the call into each `{THEM}`; `ui_svc_cw_expand()` then fills `{CALL}`.

### 9.3 Size, switches and tests

| Item | Detail |
|---|---|
| Switch | `CONFIG_CW_QSO`, = `CW_SEND` and `CW_DECODE`; [feature-matrix.md](../feature-matrix.md) has the flash figure |
| RAM | About 130 bytes: the calls offered and the call picked |
| `morseTest` | Callsign shapes taken and refused, the order, doubtful copies and our own call, five at most, `{THEM}` filling and its limit |
| `uiFlowTest` | Decode CW > OK, the calls with their count, a call into the exchange, a line sent and Back to the exchange, Back to Decode CW, Enter call into an entered call's exchange, an entry refused |

Still to come: exchange lines edited in Settings, `{RST}`, `{STATE}` and a park or summit `{REF}`, and a log of the
contacts with ADIF export.

