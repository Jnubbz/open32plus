// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The open32plus installer's logic, no DOM (installer/README.md): which image goes to the radio, with the codec slot
// that carries DMR voice, through the Baofeng serial updater. Everything that touches the radio or a codec is the
// web flasher's own code (site/static/js/flasher/), copied next to this file by tools/installer_build.py: the same
// bytes on the wire as tools/serial_loader.py, the same codec slot as tools/hrc7000_codec.py.
import * as c7 from "./js/flasher/c7000.js";
import { trimTail } from "./js/flasher/iap.js";
import { sha256 } from "./js/flasher/bytes.js";
import { BfLoader } from "./js/flasher/bfloader.js";

export const BOARD = "dm32";

export class InstallError extends Error {}

// The radio's codec layout (data/c7000.json, a cut of the site's flash-boards.json "c7000")
export function board(c) {
	const b = c.boards[BOARD];
	if (!b) throw new InstallError(`this installer's data has no ${BOARD}`);
	return b;
}

// A release image against its manifest (docs/dev/release.md): the radio, the size and the SHA-256
export async function checkRelease(manifest, image) {
	if (manifest.board !== BOARD) throw new InstallError(`the release image is for ${manifest.board}, not the ${BOARD}`);
	const want = manifest.image || {};
	if (Number(want.size) !== image.length) throw new InstallError(`the download is ${image.length} bytes, its manifest says ${want.size}: download it again`);
	if ((await sha256(image)) !== want.sha256) throw new InstallError("the download's SHA-256 doesn't match its manifest: download it again");
}

// A file the user built: the plain image (rfoundry_dm32.bin), or one merged with its codec (rfoundry_dm32_codec.bin,
// tools/hrc7000_codec.py), whose slot is split off and checked. Returns {image, slot: Uint8Array | null}
export async function splitOwn(c, data) {
	const room = board(c).slot[0] - c.app_start;
	if (data.length <= room) return { image: data, slot: null };
	const slot = data.subarray(room);
	const ok = await c7.parseSlot(c, BOARD, slot);
	if (!ok) throw new InstallError(`this file is ${data.length} bytes, more than the ${room} before the codec slot, and what follows is no codec slot: not a ${BOARD} image`);
	return { image: trimTail(data.subarray(0, room)), slot: slot.slice(0, ok.length) };
}

// The codec slot from a Baofeng stock firmware file (or a program flash backup): {slot, name}. The codec never leaves
// this computer
export async function slotFromStock(c, data) {
	const got = await c7.codecFromFile(data, c, BOARD);
	return { slot: c7.slotBytes(c, BOARD, got), name: got.profile.name };
}

// A slot kept from an earlier install, checked again: its name, or null when it is not a good slot for this radio
export async function checkSlot(c, slot) {
	const ok = slot ? await c7.parseSlot(c, BOARD, slot) : null;
	return ok ? ok.name || "codec" : null;
}

// The bytes the updater writes from the application start: the image, then (with a slot) 0xFF up to the slot and the
// slot, without the erased tail (iapui.js factoryInstallImage). The updater erases the whole application area, the slot
// included, so an install without a slot leaves the radio without DMR voice
export function installBytes(c, image, slot = null) {
	return trimTail(c7.target(c, BOARD, image, slot)[1]);
}

// The Baofeng updater on an open cable (a SerialStream), as the web flasher makes it (c7000ui.js LOADERS.baofeng)
export function loader(c, stream, log = () => {}) {
	const b = board(c);
	return new BfLoader(stream, { id: b.bootloader_id, appMax: c.app_end - c.app_start, appStart: c.app_start, protect: b.protect, log });
}

// --- The codec slot kept in this browser (IndexedDB), so the stock file is chosen once ---

const DB = "open32plus-installer", STORE = "codec", KEY = `${BOARD}-slot`;

function open() {
	return new Promise((ok, fail) => {
		const r = indexedDB.open(DB, 1);
		r.onupgradeneeded = () => r.result.createObjectStore(STORE);
		r.onsuccess = () => ok(r.result);
		r.onerror = () => fail(r.error);
	});
}

async function tx(mode, fn) {
	const db = await open();
	try {
		return await new Promise((ok, fail) => {
			const t = db.transaction(STORE, mode);
			const r = fn(t.objectStore(STORE));
			t.oncomplete = () => ok(r?.result);
			t.onerror = () => fail(t.error);
		});
	} finally {
		db.close();
	}
}

// {slot, name} or null; nothing kept (or no IndexedDB, a private window) is null
export async function loadSlot() {
	try {
		const v = await tx("readonly", (s) => s.get(KEY));
		return v && v.slot ? { slot: new Uint8Array(v.slot), name: v.name } : null;
	} catch {
		return null;
	}
}

export async function saveSlot(slot, name) {
	try {
		await tx("readwrite", (s) => s.put({ slot: slot.slice().buffer, name, saved: Date.now() }, KEY));
		return true;
	} catch {
		return false;
	}
}

export async function forgetSlot() {
	try {
		await tx("readwrite", (s) => s.delete(KEY));
	} catch { /* nothing kept */ }
}
