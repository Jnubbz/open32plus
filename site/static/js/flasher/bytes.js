// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Byte helpers shared by the flasher modules. Runs in browsers and in Node (site/tests).

export const hex = (n, width = 8) => "0x" + (n >>> 0).toString(16).toUpperCase().padStart(width, "0");

export function toHex(bytes) {
	let s = "";
	for (const b of bytes) s += b.toString(16).padStart(2, "0");
	return s;
}

export function fromHex(s) {
	const out = new Uint8Array(s.length / 2);
	for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(2 * i, 2), 16);
	return out;
}

export async function sha256(bytes) {
	return toHex(new Uint8Array(await crypto.subtle.digest("SHA-256", bytes)));
}

export const u32le = (b, off) => (b[off] | (b[off + 1] << 8) | (b[off + 2] << 16) | (b[off + 3] << 24)) >>> 0;
export const u16le = (b, off) => b[off] | (b[off + 1] << 8);
export const u16be = (b, off) => (b[off] << 8) | b[off + 1];

export function le32(n) {
	return new Uint8Array([n & 0xFF, (n >>> 8) & 0xFF, (n >>> 16) & 0xFF, (n >>> 24) & 0xFF]);
}

export function be32(n) {
	return new Uint8Array([(n >>> 24) & 0xFF, (n >>> 16) & 0xFF, (n >>> 8) & 0xFF, n & 0xFF]);
}

export function concat(...parts) {
	const n = parts.reduce((a, p) => a + p.length, 0);
	const out = new Uint8Array(n);
	let o = 0;
	for (const p of parts) {
		out.set(p, o);
		o += p.length;
	}
	return out;
}

export function filled(n, value = 0xFF) {
	return new Uint8Array(n).fill(value);
}

export function startsWith(bytes, ascii) {
	for (let i = 0; i < ascii.length; i++) if (bytes[i] !== ascii.charCodeAt(i)) return false;
	return true;
}

export function equal(a, b) {
	if (a.length !== b.length) return false;
	for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
	return true;
}

let crcTable = null;
export function crc32(bytes, crc = 0) {
	if (!crcTable) {
		crcTable = new Uint32Array(256);
		for (let n = 0; n < 256; n++) {
			let c = n;
			for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
			crcTable[n] = c >>> 0;
		}
	}
	crc = ~crc >>> 0;
	for (let i = 0; i < bytes.length; i++) crc = crcTable[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
	return ~crc >>> 0;
}

// Waits that keep their time in a hidden tab. Chrome runs a hidden tab's timers at most once a second, and after a few
// minutes once a minute, so a flash that waits 2 ms between polls of the cable took a second for each one and the radio
// gave up on the install when the window was minimized or another tab was in front. A dedicated worker's timers aren't
// held back that way, so in a browser the wait is timed there; anywhere without workers (Node, the tests) it is a plain
// setTimeout. One worker for the page, made on the first wait
let timerWorker;	// undefined: not tried yet; null: none (no Worker, or it couldn't start)
let timerSeq = 0;
const timerWaits = new Map();

function workerTimers() {
	if (timerWorker !== undefined) return timerWorker;
	timerWorker = null;
	if (typeof Worker === "undefined" || typeof Blob === "undefined" || typeof URL === "undefined" ||
	    typeof URL.createObjectURL !== "function") return null;
	try {
		const src = "onmessage = (e) => setTimeout(() => postMessage(e.data.id), e.data.ms);";
		const w = new Worker(URL.createObjectURL(new Blob([src], { type: "text/javascript" })));
		w.onmessage = (e) => {
			const done = timerWaits.get(e.data);
			timerWaits.delete(e.data);
			done?.();
		};
		w.onerror = () => {	// a page that refuses workers: the waits still pending end now, later ones use setTimeout
			timerWorker = null;
			for (const done of timerWaits.values()) done();
			timerWaits.clear();
		};
		timerWorker = w;
	} catch (e) {
		timerWorker = null;
	}
	return timerWorker;
}

export const sleep = (ms) => new Promise((r) => {
	const w = workerTimers();
	if (!w) {
		setTimeout(r, ms);
		return;
	}
	const id = ++timerSeq;
	timerWaits.set(id, r);
	w.postMessage({ id, ms });
});

export const ascii = (bytes) => new TextDecoder("latin1").decode(bytes);
