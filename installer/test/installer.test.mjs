// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The open32plus installer's logic (installer/core.js), as assembled by tools/installer_build.py, against the web
// flasher's simulated codec (c7000Simulation) and fake Baofeng updater (FakeBfLoader), for each radio the page lists.
//   python3 tools/installer_build.py --out build/installer && node --test installer/test/
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const OUT = process.env.INSTALLER_OUT || "build/installer";
const core = await import(pathToFileURL(join(OUT, "core.js")).href);
const { concat, equal } = await import(pathToFileURL(join(OUT, "js/flasher/bytes.js")).href);
const { FakeBfLoader, c7000Simulation } = await import(pathToFileURL("site/static/js/flasher/fakec7000.js").href);
const C = JSON.parse(readFileSync(join(OUT, "data/c7000.json"), "utf8"));
const RADIOS = JSON.parse(readFileSync(join(OUT, "data/radios.json"), "utf8"));
assert.ok(RADIOS.length, "the page lists radios");
// one radio of those, found by what it has (a codec profile, so a real slot can be built), not by name (AGENTS.md rule 4)
const ID = (RADIOS.find((r) => C.profiles.some((p) => p.board === r.id)) || RADIOS[0]).id;
const OTHER = RADIOS.find((r) => r.id !== ID)?.id || "another-radio";
const sim = await c7000Simulation(C, ID);

// an application-like image (c7.target refuses one byte repeated)
const app = (n, seed = 1) => {
	const a = new Uint8Array(n);
	let x = seed;
	for (let i = 0; i < n; i++) { x = (x * 1103515245 + 12345) >>> 0; a[i] = x >>> 24; }
	return a;
};

// a cable to a radio in update mode, as bfloader.test.mjs wires it
function bench() {
	const s = {
		buf: new Uint8Array(0),
		clear() { s.buf = new Uint8Array(0); },
		take(n) { const o = s.buf.slice(0, n); s.buf = s.buf.slice(n); return o; },
		async write(b) { fake.feed(b); },
	};
	const fake = new FakeBfLoader({ id: C.boards[ID].bootloader_id, send: (b) => { s.buf = concat(s.buf, b); } });
	return { s, fake };
}

test("the radio list: names and keys from the registry, only radios with the updater", () => {
	for (const r of RADIOS) {
		assert.ok(r.name && r.keys, JSON.stringify(r));
		assert.ok(C.boards[r.id]?.bootloader_id, `${r.id} has the Baofeng updater`);
	}
});

test("the codec slot from a stock file (here a simulated program flash backup) checks out", async () => {
	const { slot, name } = await core.slotFromStock(sim.c, ID, sim.progmem);
	assert.ok(slot.length > 1000);
	assert.match(name, /simulated/);
	assert.equal(await core.checkSlot(sim.c, ID, slot), name);
	assert.equal(await core.checkSlot(sim.c, ID, slot.slice(0, 64)), null, "a cut slot is no slot");
	await assert.rejects(core.slotFromStock(sim.c, ID, app(5000)), /stock firmware|codec/);
});

test("install bytes: the image alone, or the image, 0xFF to the slot and the slot", async () => {
	const { slot } = await core.slotFromStock(sim.c, ID, sim.progmem);
	const image = app(300000);
	const room = C.boards[ID].slot[0] - C.app_start;
	const plain = core.installBytes(sim.c, ID, image);
	assert.ok(equal(plain.subarray(0, image.length), image) && plain.length < room);
	const full = core.installBytes(sim.c, ID, image, slot);
	assert.equal(full.length, room + slot.length);
	assert.ok(full.subarray(image.length, room).every((b) => b === 0xFF));
	assert.ok(equal(full.subarray(room), slot));
	assert.throws(() => core.installBytes(sim.c, ID, app(room + 4)), /codec slot/);
});

test("a file I built: plain, or merged with its codec (split off and kept)", async () => {
	const { slot } = await core.slotFromStock(sim.c, ID, sim.progmem);
	const image = app(250000, 3);
	const plain = await core.splitOwn(sim.c, ID, image);
	assert.ok(plain.image === image && plain.slot === null);
	const merged = core.installBytes(sim.c, ID, image, slot);
	const got = await core.splitOwn(sim.c, ID, merged);
	assert.ok(equal(got.slot, slot));
	assert.ok(equal(core.installBytes(sim.c, ID, got.image, got.slot), merged), "installs the same bytes as the merged file");
	const room = C.boards[ID].slot[0] - C.app_start;
	await assert.rejects(core.splitOwn(sim.c, ID, app(room + 5000)), /no codec slot/);
});

test("a release image is checked against its manifest", async () => {
	const image = app(1000);
	const sha = createHash("sha256").update(image).digest("hex");
	const m = { board: ID, image: { size: image.length, sha256: sha } };
	await core.checkRelease(m, image, ID);
	await assert.rejects(core.checkRelease({ ...m, board: OTHER }, image, ID), /is for/);
	await assert.rejects(core.checkRelease(m, image.subarray(1), ID), /bytes/);
	const bad = image.slice();
	bad[10] ^= 1;
	await assert.rejects(core.checkRelease(m, bad, ID), /SHA-256/);
});

test("the whole install over the updater: the radio gets exactly the install bytes", async () => {
	const { slot } = await core.slotFromStock(sim.c, ID, sim.progmem);
	const bytes = core.installBytes(sim.c, ID, app(200000, 9), slot);
	const { s, fake } = bench();
	const ld = core.loader(sim.c, ID, s);
	ld.poll = 50;
	ld.stepTimeout = 300;
	assert.deepEqual(await ld.listen(2000), { id: C.boards[ID].bootloader_id });
	assert.throws(() => core.loader(sim.c, "no-such-radio", s), /no Baofeng updater radio/);
	const phases = [];
	const blocks = await ld.write(bytes, { onPhase: (p) => phases.push(p) });
	assert.equal(blocks, Math.ceil(bytes.length / 1024));
	assert.deepEqual(phases, ["erase", "write"]);
	assert.deepEqual(fake.errors, []);
	assert.ok(fake.done);
	assert.ok(equal(fake.image().subarray(0, bytes.length), bytes));
});

// A file named for another radio (a release asset or a build) is caught by its name; a renamed one isn't named
test("fileBoard reads the radio from a firmware file's name", () => {
	const ids = ["dm32", "dm1801pro", "db25d"];
	assert.equal(core.fileBoard("rfoundry-dm32-v26.11b1.bin", ids), "dm32");
	assert.equal(core.fileBoard("rfoundry-dm1801pro-v26.11b1.bin", ids), "dm1801pro");
	assert.equal(core.fileBoard("rfoundry-dm32-v26.11.2.bin", ids), "dm32");
	assert.equal(core.fileBoard("rfoundry_dm32.bin", ids), "dm32");
	assert.equal(core.fileBoard("rfoundry_dm32_codec.bin", ids), "dm32");
	assert.equal(core.fileBoard("rfoundry-dm32-v26.11b1 (1).bin", ids), "dm32");	// a browser's second download
	assert.equal(core.fileBoard("RFOUNDRY-DM32-V26.11B1.BIN", ids), "dm32");
	assert.equal(core.fileBoard("firmware.bin", ids), null);
	assert.equal(core.fileBoard("rfoundry-md380-v26.11b1.bin", ids), null);	// not a radio this page installs
	assert.equal(core.fileBoard("rfoundry-dm32-v26.11b1.json", ids), null);
});

