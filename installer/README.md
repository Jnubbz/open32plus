<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Josh Noble (KN0BLE) -->
# The open32plus web installer

A one-page installer for the Baofeng DM-32UV: open32plus firmware from Chrome or Edge over the programming cable,
with the DMR voice codec taken from Baofeng's own firmware file. It runs entirely in the browser and sends nothing
anywhere. It is published on GitHub Pages by `.github/workflows/installer.yml`.

## What it does

| Step | Behavior |
|---|---|
| Firmware | The newest open32plus release (built into the page, checked against its manifest's size and SHA-256), or a file the user built: `rfoundry_dm32.bin`, or `rfoundry_dm32_codec.bin` with its codec slot (split off and checked) |
| DMR voice | The codec slot from a Baofeng DM-32UV stock firmware file or program flash backup the codec recipes know (`tools/hrc7000_codec.py` profiles), kept in the browser's IndexedDB for later installs. Or none: the install then leaves the radio without DMR voice, since the updater erases the slot |
| Install | Web Serial at 115200 baud, then the Baofeng updater (`docs/spec/hardware-dm32.md` 13.2): it listens while the user switches the radio on with PTT and SK1 held, then erases and writes the image, 0xFF up to the codec slot, and the slot |

## How it is built

| Part | Source |
|---|---|
| `index.html`, `install.js`, `install.css` | The page |
| `core.js` | The logic without the DOM: release check, codec slot, install bytes, the loader, the kept codec |
| `js/` | The web flasher's modules (`site/static/js/`), copied by `tools/installer_build.py`, which follows the page's imports. The bytes on the wire are `tools/serial_loader.py`'s (`site/tests/bfloader.test.mjs`); the codec slot is `tools/hrc7000_codec.py`'s. The build service and error reporting are not among them, and the build's self-test refuses a module that names the RadioFoundry service |
| `data/c7000.json` | The site's `flash-boards.json` `c7000`: flash layout, codec profiles and maps |
| `data/release.json`, `firmware/` | The newest release's `rfoundry-dm32-<version>.bin` and `.json`, when there is a release |

```
python3 tools/installer_build.py --out build/installer [--firmware DIR]   # DIR: a release's assets
node --test installer/test/installer.test.mjs                             # against build/installer
cd build/installer && python3 -m http.server 8000                         # http://localhost:8000
```

Web Serial needs a secure context: `localhost` or HTTPS.

## Tests

`installer/test/installer.test.mjs` runs `core.js` from the built folder against the web flasher's simulated DM-32
codec (`c7000Simulation`) and fake updater (`FakeBfLoader`): the codec slot from a stock file, the install bytes with
and without it, a merged file split and written back the same, the release check, and a whole install, where the
fake radio receives exactly the install bytes. What only a radio can show: the install on the air with a real cable.

## Publishing

Settings > Pages > Source: GitHub Actions. The workflow runs on a push that changes the page or what it copies, when
a release is published, and when the Release workflow ends (a release it publishes with its own token starts no
release event).
