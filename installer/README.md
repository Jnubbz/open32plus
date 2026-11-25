<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Josh Noble (KN0BLE) -->
# The open32plus web installer

A one-page installer for the radios with the Baofeng serial updater (data/radios.json, from the board registry as
`tools/serial_loader.py` reads it; [docs/feature-matrix.md](../docs/feature-matrix.md) lists them): open32plus
firmware from Chrome or Edge over the programming cable, with the DMR voice codec taken from Baofeng's own firmware
file. It runs entirely in the browser and sends nothing
anywhere. It is published on GitHub Pages by `.github/workflows/installer.yml`.

## What it does

| Step | Behavior |
|---|---|
| Radio | A list of the radios with the updater, with each one's name, update-mode keys and sign from the registry. The last one chosen is remembered |
| Firmware | The newest open32plus release's image for that radio (built into the page, checked against its manifest's board, size and SHA-256), or a file the user built: `rfoundry_<board>.bin`, or `rfoundry_<board>_codec.bin` with its codec slot (split off and checked) |
| DMR voice | The codec slot from that radio's stock firmware file or a program flash backup the codec recipes know (`tools/hrc7000_codec.py` profiles, listed on the page), kept in the browser's IndexedDB per radio. Or none: the install then leaves the radio without DMR voice, since the updater erases the slot |
| Install | Web Serial at 115200 baud, then the Baofeng updater (the radio's hardware page, "Serial updater"): it listens while the user switches the radio on with its keys held, then erases and writes the image, 0xFF up to the codec slot, and the slot |

## The codeplug editor

The page's second page, **Channels & zones** (`editor/codeplug/`), is the web site's codeplug editor: channels, zones,
contacts, RX group lists, read from and written to a radio running open32plus over its cable, or kept as a file. It is
the site's own page, built by `site/build.py` and copied by `tools/installer_build.py --editor`:

| Part | Source |
|---|---|
| `editor/codeplug/index.html` | The site's `codeplug/index.html`, with an open32plus header (Install, Channels & zones) and footer in place of the site's, no other languages, the name |
| `editor/static/` | The site's scripts (all of `static/js/`), styles, vendor list widget and icons |
| `editor/data/` | The site's `radios.json` and `releases.json`, and its `site.json` with this repository for help and no error reporting address. Apart from the installer's `data/`, whose `radios.json` is another format |
| `editor/flash/`, `editor/docs/` | Pages the editor links to that this site doesn't have: sent on to the installer and the repository's user docs |

The editor's own tests are the site's (`site/tests/editor.test.mjs`, `codeplug.test.mjs`).

## How it is built

| Part | Source |
|---|---|
| `index.html`, `install.js`, `install.css` | The page |
| `core.js` | The logic without the DOM: release check, codec slot, install bytes, the loader, the kept codec |
| `js/` | The web flasher's modules (`site/static/js/`), copied by `tools/installer_build.py`, which follows the page's imports. The bytes on the wire are `tools/serial_loader.py`'s (`site/tests/bfloader.test.mjs`); the codec slot is `tools/hrc7000_codec.py`'s. The build service and error reporting are not among them, and the build's self-test refuses a module that names the RadioFoundry service |
| `data/radios.json` | The radios with the updater: id, name, keys, sign (`tools/serial_loader.py` `MODELS`) |
| `data/c7000.json` | The site's `flash-boards.json` `c7000`: flash layout, codec profiles and maps |
| `data/release.json`, `firmware/` | The newest release's `rfoundry-<board>-<version>.bin` and `.json` for each of those radios, when there is a release |

```
python3 site/build.py --out build/site                                    # for --editor
python3 tools/installer_build.py --out build/installer [--firmware DIR] [--editor build/site]   # DIR: a release's assets
node --test installer/test/installer.test.mjs                             # against build/installer
cd build/installer && python3 -m http.server 8000                         # http://localhost:8000
```

Web Serial needs a secure context: `localhost` or HTTPS.

## Tests

`installer/test/installer.test.mjs` runs `core.js` from the built folder against the web flasher's simulated codec
(`c7000Simulation`) and fake updater (`FakeBfLoader`), for a radio of the list found by what it has: the radio list,
the codec slot from a stock file, the install bytes with and without it, a merged file split and written back the
same, the release check, and a whole install, where the fake radio receives exactly the install bytes. What only a
radio can show: an install with a real cable.

## Publishing

Settings > Pages > Source: GitHub Actions. The workflow runs on a push that changes the page or what it copies, when
a release is published, and when the Release workflow ends (a release it publishes with its own token starts no
release event).
