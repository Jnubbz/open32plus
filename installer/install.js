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
	radios: [],		// data/radios.json: [{id, name, keys, sign}], the radios with the Baofeng updater
	radio: null,		// the one chosen
	release: null,		// data/release.json ({version, date, notes, images: {board: {image, manifest}}}), or null
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
	for (const id of ["radio", "pick-release", "pick-own", "codec-pick", "codec-skip"]) $(id).disabled = S.busy;
	if (!S.fw) setStatus("install-status", "Choose the firmware first.");
	else if (!codecOk) setStatus("install-status", "Choose the Baofeng firmware file for DMR voice, or install without it.");
	else if (!S.busy && !$("install-status").dataset.kept) setStatus("install-status", "Ready. Click Connect.");
}

// ---- 1. Firmware ----

const releaseFor = (id) => S.release?.images?.[id] || null;

async function useRelease() {
	S.fw = null;
	refresh();
	const r = S.release, files = releaseFor(S.radio.id);
	setStatus("fw-status", `Downloading ${r.version}…`);
	try {
		const [manifest, image] = await Promise.all([getJson(files.manifest), getBytes(files.image)]);
		await core.checkRelease(manifest, image, S.radio.id);
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
		// A file named for another radio is refused before anything reaches the radio: its bootloader would erase the
		// application area first, and only then refuse the image
		const named = core.fileBoard(file.name, S.radios.map((r) => r.id));
		if (named && named !== S.radio.id) {
			const other = S.radios.find((r) => r.id === named);
			throw new core.InstallError(`${file.name} is for the ${other?.name || named}, not the ${S.radio.name}. Choose the file with ${S.radio.id} in its name.`);
		}
		const data = await readFile(file);
		const { image, slot } = await core.splitOwn(S.c, S.radio.id, data);
		S.fw = { image, slot, source: file.name };
		let text = `${file.name}: ${kb(image.length)}.`;
		if (slot) {
			const name = await core.checkSlot(S.c, S.radio.id, slot);
			text += ` It carries its own codec (${name}), which is installed with it.`;
			S.codec = { slot, name, from: "image" };
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
		const { slot, name } = await core.slotFromStock(S.c, S.radio.id, await readFile(file));
		S.codec = { slot, name, from: "file" };
		codecShow();
		if (await core.saveSlot(S.radio.id, slot, name)) log(`codec ${name} kept in this browser for the next install`);
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

// While an install runs: the screen and the computer kept awake (a computer that sleeps drops the cable), a warning
// before the page is closed or reloaded, and the tab's title saying so while it is hidden. Waits are timed in a worker
// (bytes.js sleep), so a hidden tab keeps going, but the radio still stops if the computer sleeps or the tab closes
const guard = {
	lock: null,
	title: document.title,
	async awake() {
		try {
			if (S.busy && document.visibilityState === "visible" && !this.lock) {
				this.lock = await navigator.wakeLock?.request("screen");
				this.lock?.addEventListener("release", () => { this.lock = null; });
			}
		} catch { /* not granted (battery saver, an old browser): the warning still says so */ }
	},
	start() {
		this.awake();
		window.addEventListener("beforeunload", this.unload);
		document.addEventListener("visibilitychange", this.shown);
	},
	stop() {
		window.removeEventListener("beforeunload", this.unload);
		document.removeEventListener("visibilitychange", this.shown);
		document.title = this.title;
		this.lock?.release().catch(() => {});
		this.lock = null;
	},
	unload(e) {
		e.preventDefault();
		e.returnValue = "";
	},
	shown() {
		const hidden = document.visibilityState === "hidden";
		document.title = hidden ? "⚠ Installing: keep this tab open" : guard.title;
		if (hidden) log("the page went to the background (installing goes on; keep the tab open and the computer awake)");
		else guard.awake();	// a wake lock ends when the page is hidden: taken again
	},
};

async function install() {
	const api = serialApi();
	let stream = null;
	let erased = false;	// past step 7: a stop leaves the radio without firmware until an install completes
	S.busy = true;
	guard.start();
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
		const bytes = core.installBytes(S.c, S.radio.id, S.fw.image, slot);
		log(`image ${S.fw.source}, ${S.fw.image.length} bytes${slot ? `, codec slot ${slot.length} bytes` : ", no codec"}: ${bytes.length} bytes to write`);
		const port = await api.requestPort();
		setStatus("install-status", "Opening the cable…");
		stream = await SerialStream.open(port, 115200);
		const ld = core.loader(S.c, S.radio.id, stream, log);
		setStatus("install-status", `Waiting for update mode: switch the radio on with ${S.radio.keys} held.`);
		const id = await ld.listen(120000, { signal: S.stop.signal });
		log(`update mode found (${id.id})`);
		$("progress").hidden = false;
		$("progress").value = 0;
		const blocks = await ld.write(bytes, {
			name: `rfoundry_${S.radio.id}.bin`,
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
		guard.stop();
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
	try {
		S.radios = (await getJson("data/radios.json")).filter((r) => S.c.boards[r.id]?.bootloader_id);
	} catch (e) {
		setStatus("fw-status", `This page didn't load its radio list: ${e.message}. Reload the page.`, "bad");
		return;
	}
	const sel = $("radio");
	for (const r of S.radios) sel.append(new Option(r.name, r.id));
	// the radio chosen last time, else the first with a release
	let first = null;
	try { first = localStorage.getItem("open32plus-radio"); } catch { /* no storage */ }
	if (!S.radios.some((r) => r.id === first)) first = (S.radios.find((r) => releaseFor(r.id)) || S.radios[0])?.id;
	sel.value = first;
	sel.addEventListener("change", () => chooseRadio(sel.value));
	$("pick-release").addEventListener("change", () => useRelease());
	$("pick-own").addEventListener("click", () => $("own-file").click());
	$("own-file").addEventListener("change", (e) => e.target.files[0] && useOwn(e.target.files[0]));
	$("codec-pick").addEventListener("click", () => $("codec-file").click());
	$("codec-file").addEventListener("change", (e) => e.target.files[0] && useStock(e.target.files[0]));
	$("codec-forget").addEventListener("click", async () => {
		await core.forgetSlot(S.radio.id);
		S.codec = null;
		codecShow();
		refresh();
	});
	$("codec-skip").addEventListener("change", refresh);
	$("connect").addEventListener("click", install);
	document.querySelector(".theme-toggle")?.addEventListener("click", () => {
		const root = document.documentElement;
		const dark = root.dataset.theme ? root.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
		root.dataset.theme = dark ? "light" : "dark";
		try { localStorage.setItem("rfoundry-theme", root.dataset.theme); } catch { /* private mode: not remembered */ }
	});
	$("stop").addEventListener("click", () => S.stop?.abort());
	await chooseRadio(first);
}

// A radio chosen: its keys and sign, its release image, its kept codec and the stock versions it knows
async function chooseRadio(id) {
	S.radio = S.radios.find((r) => r.id === id);
	try { localStorage.setItem("open32plus-radio", id); } catch { /* no storage */ }
	for (const el of document.querySelectorAll(".radio-name")) el.textContent = S.radio.name;
	for (const el of document.querySelectorAll(".bin-name")) el.textContent = `rfoundry_${id}`;
	$("keys").textContent = S.radio.keys;
	$("sign").textContent = S.radio.sign ? ` (${S.radio.sign.charAt(0).toLowerCase()}${S.radio.sign.slice(1).replace(/\.$/, "")})` : "";
	$("codec-versions").textContent = (S.c.profiles || []).filter((p) => p.board === id).map((p) => p.name.replace(/\.bin$/, "")).join(", ") || "none yet";
	S.fw = null;
	S.codec = null;
	$("own-file").value = "";
	$("codec-file").value = "";
	if (releaseFor(id)) {
		$("pick-release-row").hidden = false;
		$("rel-version").textContent = S.release.version;
		$("rel-date").textContent = S.release.date ? `(${S.release.date})` : "";
		$("rel-notes").href = S.release.notes;
		$("pick-release").checked = true;
		useRelease();
	} else {
		$("pick-release-row").hidden = true;
		$("pick-own").checked = false;
		setStatus("fw-status", S.release ? `The newest release has no image for the ${S.radio.name}: choose a file you built.` : "No release yet: choose a file you built.");
	}
	const kept = await core.loadSlot(id);
	const name = kept && (await core.checkSlot(S.c, id, kept.slot));
	if (name) S.codec = { slot: kept.slot, name: kept.name || name, from: "browser" };
	codecShow();
	refresh();
}

start();
