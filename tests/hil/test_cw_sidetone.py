# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""The CW sidetone (docs/spec/morse.md 5.2) on a radio whose board has one (cw.side_hw): the speaker plays the keyed
tone while it goes out, and the tone on air is the same with the sidetone on as with it off.

Opt-in: radio_tx + bench_service (--allow-rf --allow-service-stop), into a dummy load. The radio's FM VFO is tuned to
--fm-mhz on its lowest power, no subtone (test_fm_tx.py's fm_vfo); the first side key is set to Straight key
(restored afterwards) and its scan bit held 600 ms through the debug link, once with Settings > CW > Sidetone on and
once off. The bench only receives: its board demodulates each over (SdrBench.fm_over_start).

Pass criteria: with the sidetone on, the board's speaker amplifier has its sidetone user (board.amp_users bit 0x40,
on a board whose view reports it) while the key is held, and not with it off; each over holds a run of the CW tone
(cw.tone_hz, 800 Hz by default, +-15 Hz) of 0.45-0.8 s; the two runs' deviations within 1 dB of each other (the
amplifier doesn't load the transmit audio). Listening to the speaker is the feature's manual step."""
import time

import pytest

from rfoundry_hil import as_int, skip
from hil_config import rig
from test_fm_tx import fm_vfo, key_and_check, set_fm, set_first_side_key  # noqa: F401 (fm_vfo is a fixture)

KEYFN_CW_KEY = 22   # core/input/keyassign.h: Straight key
AMP_SIDE = 0x40     # the board's sidetone user of the speaker amplifier, where its view reports amp_users


def set_sidetone(radio, on):
    """Settings > CW > Sidetone, its fourth row (Speed, Farnsworth, Tone, Sidetone), toggled until cw.sidetone is on"""
    want = "1" if on else "0"
    if radio.view(["cw"]).get("cw.sidetone") == want:
        return
    radio.go_home()
    for key in ("ok", "7", "up", "up", "up", "ok", "down", "down", "down", "ok"):
        radio.press(key)
    try:
        radio.wait_for(lambda v: v.get("cw.sidetone") == want, sections=("cw",),
                       what=f"Settings > CW > Sidetone {'on' if on else 'off'}")
    finally:
        radio.go_home()


def over(radio, sdr_bench, pytestconfig, bit):
    """The straight key held 600 ms; the board's amplifier users seen while it was held, and the board's report"""
    seen = []

    def key():
        radio.link.keys_mask(1 << bit, 600)
        radio.wait_for(lambda v: v.get("state") == "tx", 0.6, interval=0.03, what="straight key keyed")
        end = time.monotonic() + 0.4
        while time.monotonic() < end:
            amp = radio.view(["board"]).get("board.amp_users")
            if amp is not None:
                seen.append(int(amp, 16))
            time.sleep(0.05)
        radio.link.wait_keys_idle()

    r = key_and_check(radio, sdr_bench, pytestconfig, 12.5, key=key, seconds=5, tones=True)
    return seen, r


def cw_run(r, hz):
    runs = [t for t in r["tones"] if abs(t[0] - hz) <= 15.0]
    assert runs, f"no {hz} Hz tone on air: {r['tones']}"
    _, t0, t1, dev = max(runs, key=lambda t: t[2] - t[1])
    assert 0.45 <= t1 - t0 <= 0.8, f"the 600 ms key held sent {hz} Hz for {t1 - t0:.2f} s"
    return dev


@pytest.mark.feature("cw_sidetone")
@pytest.mark.radio_tx
@pytest.mark.bench_service
@pytest.mark.sdr_rx
@pytest.mark.needs_switch("VFO_FM")
def test_cw_sidetone(fm_vfo, sdr_bench, pytestconfig):  # noqa: F811
    radio = fm_vfo
    for feature in ("CW_SEND", "KEY_ASSIGN", "UI_SETTINGS"):
        if feature not in radio.features():
            skip("not_built", f"built without CONFIG_{feature}")
    cw = radio.view(["cw"])
    if "cw.side_hw" not in cw:
        skip("not_built", "firmware older than the CW sidetone")
    if cw["cw.side_hw"] != "1":
        skip("no_hw", "this radio's board has no CW sidetone")
    hz_tone = as_int(cw.get("cw.tone_hz"), 0) or 800
    link = radio.view(["link"])
    if "key.first.bit" not in link:
        skip("not_built", "no assignable side key")
    bit, saved_key = int(link["key.first.bit"]), int(link["key.first.press"])
    saved_side = cw.get("cw.sidetone") == "1"
    hz = round(rig(pytestconfig, "fm-mhz") * 1e6)
    try:
        set_first_side_key(radio, KEYFN_CW_KEY)
        devs = {}
        for on in (True, False):
            set_sidetone(radio, on)
            # A settings save can reload the codeplug: back to the test's FM VFO, no tone, lowest power
            radio.link.dmr_fields(mode="fm")
            radio.link.vfo_set(hz)
            radio.wait_for(lambda v: v.get("mode") == "fm" and v.get("rx_hz") == str(hz), what="FM test frequency")
            radio.lowest_power()
            set_fm(radio, False, "none", 0)
            seen, r = over(radio, sdr_bench, pytestconfig, bit)
            if seen:
                side = any(a & AMP_SIDE for a in seen)
                assert side == on, f"sidetone {'on' if on else 'off'}: amplifier users {[hex(a) for a in seen]}"
            devs[on] = cw_run(r, hz_tone)
        ratio = devs[True] / max(devs[False], 1.0)
        print(f"deviation with the sidetone {devs[True]} Hz, without {devs[False]} Hz")
        assert 0.89 <= ratio <= 1.12, f"the sidetone changed the deviation: {devs[True]} vs {devs[False]} Hz"
    finally:
        try:
            radio.link.wait_keys_idle()
            set_sidetone(radio, saved_side)
        finally:
            set_first_side_key(radio, saved_key)
