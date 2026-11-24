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
| Settings | Settings > CW, between My radio and Signaling tones (no digit key, so the digit keys' groups keep theirs): Speed 5..40 wpm (`cw_wpm`, 0 = 20), Farnsworth Off or 5..18 wpm (`cw_eff_wpm`, CW messages only; the CW IDs keep standard spacing), Tone 400..1000 Hz (`cw_tone_hz`, 0 = 800) |

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
| Keying | `cw_begin()`, as the CW IDs: carrier up, a 600 ms lead (`CW_MSG_LEAD_MS`; an ID's is 150 ms), the Morse on `tx_tone` / `tx_tone_key`, 150 ms tail. The view: `RS_TX`, `tx_auto`, `cw_tx` | One keyer for every CW path. The receivers are handhelds: squelch, battery save and a subtone decoder open on the carrier, and with 150 ms the first letter was lost on the air |
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
| Settings | `cw_eff_wpm` in the old `reserved10[1]` (page 1, offset 399; [settings-store.md](settings-store.md)) |

### 5.4 Tests and debugging

| Test | Checks |
|---|---|
| `sharedPttTest` | A press during a transmission counts only while `ptt_stops_tx` is on |
| `radioCoreTest` | Keying "AB" (the 600 ms lead, element timing, tail, back to the channel), PTT cutting it without keying, `ptt_stops_tx` on only while it keys, stop, a channel change, every refusal, the channel's bandwidth and CTCSS, Farnsworth and tone from the settings, the UI services and settings |
| `morseTest` | `morse_ms()` against the keyed timelines, `radio_cwmsg_clean()` and `radio_cwmsg_expand()` |
| `uiFlowTest` | The Messages row, `cw-compose`, `cw-send`, Back (to the CW list, then Messages), Write..., Settings > CW |
| Debug link | View section 39 `cw` ([debuglink.md](debuglink.md)) |
| HIL | `tests/hil/features.toml` `cw_send`: opt-in, `--allow-rf` only, into a dummy load |

## 6. Tests

`tests/host/morseTest.c`: punctuation, prosigns, Farnsworth gaps and keyed timelines, and a 160 character buffer.
`tests/host/mmdvmTest.c`: PARIS at 20 wpm, the code table and keying over time for the CW ID. CW messages: 5.4.
