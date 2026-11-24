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

## 5. Tests

`tests/host/morseTest.c`: punctuation, prosigns, Farnsworth gaps and keyed timelines, and a 160 character buffer.
`tests/host/mmdvmTest.c`: PARIS at 20 wpm, the code table and keying over time for the CW ID.
