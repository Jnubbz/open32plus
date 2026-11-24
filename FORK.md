<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Josh Noble (KN0BLE) -->

# open32plus

open32plus is a fork of [RadioFoundry](https://github.com/RadioFoundry/RadioFoundry), MIT licensed. It is not an
official RadioFoundry release. RadioFoundry's `LICENSE`, `AUTHORS`, `CONTRIBUTORS` and every copyright line stay as
they are ([FORKING.md](FORKING.md)). Files we change get our copyright line below theirs.

## What open32plus adds

| Area | Goal |
|---|---|
| CW, sending | Keyboard-to-CW from the message editor, CW macros (`{CALL}`, `{THEIR}`, `{RST}`), MCW over FM, Farnsworth spacing, prosigns, beacon/repeat mode |
| CW, carrier | True carrier CW with shaped (click-free) keying, if the RF chip allows it |
| CW, receive | CW receive and an on-screen decoder into the message inbox |
| CW, interface | Morse menu prompts, a built-in Koch trainer, side keys or a paddle as the key |
| UX | Menu and workflow changes for operating, not just configuring |
| Web tools | Channel generation by ZIP/county, built into the browser tooling |

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
