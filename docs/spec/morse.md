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

`morse_t` holds a copy of the text, at most `MORSE_TEXT_MAX` characters (default 40, the CW ID's limit). Longer
text is cut. A build that keys longer messages defines `MORSE_TEXT_MAX` higher, up to 255.

## 5. Sending a message as CW (planned, `CONFIG_CW_SEND`)

Status: design. Nothing below is implemented yet. It follows the APRS transmission's model
([aprs.md](aprs.md) 6): a request from the UI, keyed by the radio core from idle, then back to the channel.

### 5.1 What the user does

| Step | Behavior |
|---|---|
| Open | Messages > Send CW opens the composer in CW mode (`compose`, a third mode next to DMR and APRS): canned CW messages, then Write... |
| Canned | `CQ CQ CQ DE {CALL} {CALL} K`, `QRZ? DE {CALL} K`, `TU 73 DE {CALL} <SK>`, `DE {CALL}` |
| Write | The text editor; the text is upper-cased and anything without a code is dropped before keying |
| Send | OK keys the message on the current channel. A status line shows it is sending. PTT or Back stops it at once |
| Settings | Settings > CW: speed (`cw_wpm`), Farnsworth speed (new, `cw_eff_wpm`, 0 = off), tone (`cw_tone_hz`). The hotspot and SFR CW IDs keep standard spacing |

`{CALL}` is the callsign from the radio name (`radio_cwid_callsign`, the SFR CW ID's rule). A canned message that
needs `{CALL}` is refused with a toast when the radio name holds no callsign.

### 5.2 Where and how it keys

| Rule | Behavior | Why |
|---|---|---|
| Channel | Only an FM channel. A DMR, P25 or NXDN channel, the air band and broadcast FM refuse it with a toast | MCW is analog FM; keying it on a digital channel's frequency would put FM onto a digital repeater's input |
| Frequency, power | The channel's TX frequency, bandwidth and power | The user picked the channel |
| Subtone | The channel's TX CTCSS/DCS goes out with the tone | A tone-access repeater opens for it |
| Start | Only from idle, by the same rules as an APRS transmission (`aprs_can_start`): no PTT, no reception, no scan, no hotspot, no DMR call or data, no VOX | Never steps on traffic or another keying |
| Guards | Through `key_tx()`: TX inhibit, a pending hardware option, a critical battery and the test TX limit refuse it | Every keying path shares them |
| Length | Refused when the message at the set speed takes longer than the channel's TOT (`morse_units`, and the Farnsworth gaps) | The TOT is never the thing that ends a CW message |
| Keying | `cw_begin()` as the CW IDs: carrier up, 150 ms lead, the Morse on `tx_tone` / `tx_tone_key`, 150 ms tail | One keyer for every CW path |
| End | `tune()` back to the channel, as an APRS transmission ends | The channel exactly as before |
| Stop | PTT, Back on the status line, a channel change or any other request cuts it: key up at once, then `tune()` | The user is always in control of the transmitter |

### 5.3 Size and switches

| Item | Cost |
|---|---|
| `CONFIG_CW_SEND` | `config/features.mk` and `core/config.h`, off on boards short of flash or RAM; needs `tx_tone` |
| `RADIO_SM_CW` | Also on with `CONFIG_CW_SEND` |
| Text | `MORSE_TEXT_MAX` 144 with the switch on (the message editor's `UI_MSG_TEXT_MAX`, 143): about 104 bytes more in `radio_sm_t` |
| Settings | `cw_eff_wpm` appended to the store record that holds `cw_wpm` and `cw_tone_hz` ([settings-store.md](settings-store.md)) |

### 5.4 Tests

| Test | Checks |
|---|---|
| Host, radio core | Refusals (not FM, busy, inhibit, too long for the TOT), the keying order (lead, Morse, tail), PTT cutting it, `tune()` at the end |
| Host, UI | A `uiFlowTest` flow that opens the CW composer and checks `screen=compose`; canned rows with `{CALL}` filled in |
| Debug link | `cw.send_requests`, `cw.sent`, `cw.cut`, `cw.refused` view keys |
| HIL | Opt-in, `--allow-rf` only, into a dummy load: the keyed time matches `morse_units` at the set speed |

## 6. Tests

`tests/host/morseTest.c`: punctuation, prosigns, Farnsworth gaps and keyed timelines, and a 160 character buffer.
`tests/host/mmdvmTest.c`: PARIS at 20 wpm, the code table and keying over time for the CW ID.
