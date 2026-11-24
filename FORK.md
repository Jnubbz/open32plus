<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Josh Noble (KN0BLE) -->

# open32plus

open32plus is a fork of [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry), MIT licensed. It is not an
official RadioFoundry release. RadioFoundry's `LICENSE`, `AUTHORS`, `CONTRIBUTORS` and every copyright line stay as
they are ([FORKING.md](FORKING.md)). Files we change get our copyright line below theirs.

## Install

Open the installer in Chrome or Edge on a computer: **https://jnubbz.github.io/open32plus/**. It installs the newest
release over the programming cable and keeps DMR voice working with the codec from Baofeng's own firmware file
([installer/README.md](installer/README.md)).

## What open32plus adds

| Area | Goal |
|---|---|
| CW, sending | Done: CW messages from the message editor (canned or typed, `{CALL}`), MCW over FM with the channel's CTCSS, Farnsworth spacing, prosigns, a lead-in setting. Still to do: beacon/repeat mode, more macros |
| CW, carrier | True carrier CW with shaped (click-free) keying, if the RF chip allows it |
| CW, receive | Done: Messages > Decode CW, an MCW decoder on the FM channel |
| CW, interface | Morse menu prompts, a built-in Koch trainer, side keys or a paddle as the key |
| UX | Menu and workflow changes for operating, not just configuring |
| Web tools | Done: the web installer. Still to do: channel generation by ZIP/county |

## How the fork is run

- `main` follows upstream. Sync with GitHub's **Sync fork** button or
  `git fetch upstream && git merge upstream/main`.
- Our work lives on feature branches (for example `cw`) and merges into `main` when it is ready.
- We follow RadioFoundry's rules ([AGENTS.md](AGENTS.md), [docs/CLEANROOM.md](docs/CLEANROOM.md)): clean room,
  SPDX headers, generic code in `core/`, host tests for new behavior, zero warnings. That keeps every addition
  ready to offer upstream.
- Shared behavior extends the existing modules (for example `core/radio/morse.c`) instead of adding a second copy.

## Safety

Never transmit untested code on the air. Test into a dummy load with an SDR watching first, and only on a radio you
own that is not used on a licensed commercial (Part 90) system.
