// SPDX-License-Identifier: MIT
// Copyright (c) 2026 The RadioFoundry Authors
// Copyright (c) 2026 Josh Noble (KN0BLE)
// The open32plus installer page (installer/index.html, installer/README.md): firmware, DMR voice, install.
import * as core from "./core.js";
import { SerialStream, serialApi } from "./js/flasher/link.js";
import { BfError } from "./js/flasher/bfloader.js";

const $ = (id) => document.getElementById(id);
const S = {
	c: null,		// data/c7000.json
	release: null,		// data/release.json, or null when there is none yet
	fw: null,		// {image, slot, source}
	codec: null,		// {slot, name}: from the stock file or kept in this browser
	busy: false,
	stop: null,		// AbortController of the running install
};

function setStatus(id, text, kind = "") {
	const el = $(id);
	el.textContent = text;
	el.className = `status ${kind}`;
}

function log(line) {
	const el = $("log");
	el.textContent += `${line}\n`;
	el.scrollTop = el.scrollHeight;
}

const kb = (n) => `${Math.round(n / 1024)} KB`;
const readFile = (f) => f.arrayBuffer().then((b) => new Uint8Array(b));

async function getJson(path) {
	const r = await fetch(path, { cache: "no-cache" });
	if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
	return r.json();
}

async function getBytes(path) {
	const r = await fetch(path, { cache: "no-cache" });
	if (!r.ok) throw new Error(`${path}: HTTP ${r.status}`);
	return new Uint8Array(await r.arrayBuffer());
}

// What the Install step can do now
function refresh() {
	const codecOk = !!S.codec || $("codec-skip").checked;
	$("connect").disabled = S.busy || !S.fw || !codecOk || !serialApi();
	$("stop").hidden = !S.busy;
	$("codec-forget").hidden = !S.codec || S.codec.from !== "browser";
	for (const id of ["pick-release", "pick-own", "codec-pick", "codec-skip"]) $(id).disabled = S.busy;
	if (!S.fw) setStatus("install-status", "Choose the firmware first.");
	else if (!codecOk) setStatus("install-status", "Choose the Baofeng firmware file for DMR voice, or install without it.");
	else if (!S.busy && !$("install-status").dataset.kept) setStatus("install-status", "Ready. Click Connect.");
}

// ---- 1. Firmware ----

async function useRelease() {
	S.fw = null;
	refresh();
	const r = S.release;
	setStatus("fw-status", `Downloading ${r.version}…`);
	try {
		const [manifest, image] = await Promise.all([getJson(r.manifest), getBytes(r.image)]);
		await core.checkRelease(manifest, image);
		S.fw = { image, slot: null, source: `open32plus ${r.version}` };
		setStatus("fw-status", `open32plus ${r.version}: ${kb(image.length)}, checked against its manifest.`, "ok");
	} catch (e) {
		setStatus("fw-status", `The release didn't download: ${e.message}`, "bad");
	}
	refresh();
}

async function useOwn(file) {
	S.fw = null;
	try {
		const data = await readFile(file);
		const { image, slot } = await core.splitOwn(S.c, data);
		S.fw = { image, slot, source: file.name };
		let text = `${file.name}: ${kb(image.length)}.`;
		if (slot) {
			text += ` It carries its own codec (${await core.checkSlot(S.c, slot)}), which is installed with it.`;
			S.codec = { slot, name: await core.checkSlot(S.c, slot), from: "image" };
			codecShow();
		}
		setStatus("fw-status", text, "ok");
	} catch (e) {
		setStatus("fw-status", e.message, "bad");
	}
	refresh();
}

// ---- 2. DMR voice ----

function codecShow() {
	if (!S.codec) {
		setStatus("codec-status", "No codec yet.");
		return;
	}
	const where = { browser: "kept in this browser from an earlier install", file: "read from the file", image: "in the firmware file you chose" }[S.codec.from];
	setStatus("codec-status", `Codec: ${S.codec.name}, ${where}. DMR voice will work.`, "ok");
}

async function useStock(file) {
	try {
		const { slot, name } = await core.slotFromStock(S.c, await readFile(file));
		S.codec = { slot, name, from: "file" };
		codecShow();
		if (await core.saveSlot(slot, name)) log(`codec ${name} kept in this browser for the next install`);
	} catch (e) {
		setStatus("codec-status", `${file.name}: ${e.message}`, "bad");
	}
	refresh();
}

// ---- 3. Install ----

function explain(e) {
	if (e?.name === "NotFoundError") return "No port was chosen.";
	if (e?.name === "NetworkError" || /lost/i.test(e?.message || "")) return "The cable was unplugged or the port closed. Plug it in and try again.";
	if (e?.name === "InvalidStateError" || /already open/i.test(e?.message || "")) return "The port is in use, maybe by CPS or another tab. Close that and try again.";
	return e?.message || String(e);
}

async function install() {
	const api = serialApi();
	let stream = null;
	let erased = false;	// past step 7: a stop leaves the radio without firmware until an install completes
	S.busy = true;
	S.stop = new AbortController();
	delete $("install-status").dataset.kept;
	$("progress").hidden = true;
	$("log").textContent = "";
	refresh();
	const done = (text, kind) => {
		setStatus("install-status", text, kind);
		$("install-status").dataset.kept = "1";
	};
	try {
		const slot = S.fw.slot || ($("codec-skip").checked ? null : S.codec?.slot) || null;
		const bytes = core.installBytes(S.c, S.fw.image, slot);
		log(`image ${S.fw.source}, ${S.fw.image.length} bytes${slot ? `, codec slot ${slot.length} bytes` : ", no codec"}: ${bytes.length} bytes to write`);
		const port = await api.requestPort();
		setStatus("install-status", "Opening the cable…");
		stream = await SerialStream.open(port, 115200);
		const ld = core.loader(S.c, stream, log);
		setStatus("install-status", "Waiting for update mode: switch the radio on with PTT and SK1 held.");
		const id = await ld.listen(120000, { signal: S.stop.signal });
		log(`update mode found (${id.id})`);
		$("progress").hidden = false;
		$("progress").value = 0;
		const blocks = await ld.write(bytes, {
			name: "rfoundry_dm32.bin",
			signal: S.stop.signal,
			onPhase: (p) => (erased = true) && setStatus("install-status", p === "erase"
				? "Erasing the application area (about 15 seconds)… Don't switch the radio off."
				: "Writing… Don't switch the radio off or unplug the cable."),
			onPage: (i, n) => {
				$("progress").value = Math.round((100 * i) / n);
			},
		});
		log(`${blocks} blocks written`);
		done(`Installed ${S.fw.source}${slot ? " with DMR voice" : " without DMR voice"}. The radio restarts by itself: check Menu → Settings → My radio → About.`, "ok");
	} catch (e) {
		let text = e instanceof BfError && e.message === "stopped" ? "Stopped." : explain(e);
		if (erased && !/erased now/.test(text))
			text += " The radio's firmware was erased: it won't start until an install completes. Enter update mode and install again.";
		log(`error: ${e?.message || e}`);
		done(text, "bad");
	} finally {
		if (stream) await stream.close().catch(() => {});
		S.busy = false;
		S.stop = null;
		refresh();
	}
}

// ---- Start ----

async function start() {
	if (!serialApi()) $("nosupport").hidden = false;
	try {
		S.c = await getJson("data/c7000.json");
		core.board(S.c);
	} catch (e) {
		setStatus("fw-status", `This page didn't load its radio data: ${e.message}. Reload the page.`, "bad");
		return;
	}
	try {
		const info = await getJson("data/site.json");
		$("site-build").textContent = `page ${info.commit}`;
	} catch { /* a local build without it */ }
	try {
		S.release = await getJson("data/release.json");
	} catch {
		S.release = null;
	}
	if (S.release) {
		$("pick-release-row").hidden = false;
		$("rel-version").textContent = S.release.version;
		$("rel-date").textContent = S.release.date ? `(${S.release.date})` : "";
		$("rel-notes").href = S.release.notes;
		$("pick-release").checked = true;
		useRelease();
	} else {
		setStatus("fw-status", "No release yet: choose a file you built.");
	}
	const kept = await core.loadSlot();
	const name = kept && (await core.checkSlot(S.c, kept.slot));
	if (name) S.codec = { slot: kept.slot, name: kept.name || name, from: "browser" };
	codecShow();

	$("pick-release").addEventListener("change", () => useRelease());
	$("pick-own").addEventListener("click", () => $("own-file").click());
	$("own-file").addEventListener("change", (e) => e.target.files[0] && useOwn(e.target.files[0]));
	$("codec-pick").addEventListener("click", () => $("codec-file").click());
	$("codec-file").addEventListener("change", (e) => e.target.files[0] && useStock(e.target.files[0]));
	$("codec-forget").addEventListener("click", async () => {
		await core.forgetSlot();
		S.codec = null;
		codecShow();
		refresh();
	});
	$("codec-skip").addEventListener("change", refresh);
	$("connect").addEventListener("click", install);
	$("stop").addEventListener("click", () => S.stop?.abort());
	refresh();
}

start();
