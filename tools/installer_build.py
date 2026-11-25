#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Josh Noble (KN0BLE)
"""Assemble the open32plus web installer (installer/README.md) into one static folder.
  tools/installer_build.py --out build/installer [--firmware DIR] [--commit SHA]
The page (installer/), the web flasher modules it imports (site/static/js/, followed import by import, so the copy
holds what runs and nothing else), the radios with the Baofeng serial updater (data/radios.json, from the board
registry as tools/serial_loader.py reads it), their flash layout (data/c7000.json, the site's flash-boards.json
"c7000") and, with --firmware, each such radio's image and manifest from DIR (a release's assets:
rfoundry-<board>-<version>.bin and .json), described in data/release.json. Without one the page installs only a file
the user picks.
With --editor SITE (a site/build.py output), the codeplug editor joins it under editor/: the site's codeplug page with
an open32plus header and footer, the site's scripts and styles, and its radio list, so the page edits channels, zones
and contacts on a radio over its cable without RadioFoundry's web site.
  tools/installer_build.py --self-test
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "installer")
JS = os.path.join(ROOT, "site", "static", "js")
PAGE = ["index.html", "install.js", "install.css", "core.js"]
ENTRIES = ["install.js", "core.js"]
REPO_URL = "https://github.com/Jnubbz/open32plus"
IMPORT_RE = re.compile(r'(?:^|[;\s])(?:import|export)\b[^;"\']*?from\s*["\'](\.{1,2}/[^"\']+)["\']|import\(\s*["\'](\.{1,2}/[^"\']+)["\']\s*\)', re.M)


def imports(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return [a or b for a, b in IMPORT_RE.findall(text)]


def modules():
    """The site modules the page imports, followed through their own imports: paths under site/static/js"""
    todo, seen = [], set()
    for e in ENTRIES:
        for rel in imports(os.path.join(SRC, e)):
            if not rel.startswith("./js/"):
                continue
            todo.append(os.path.normpath(os.path.join(JS, rel[len("./js/"):])))
    while todo:
        p = todo.pop()
        if p in seen:
            continue
        if not os.path.isfile(p):
            sys.exit("installer_build: %s imports %s, which doesn't exist" % (sorted(seen)[-1:] or "the page", p))
        seen.add(p)
        for rel in imports(p):
            todo.append(os.path.normpath(os.path.join(os.path.dirname(p), rel)))
    return sorted(seen)


EDITOR = "editor"
EDITOR_IMG = ["icon.svg", "icon-32.png", "apple-touch-icon.png"]
EDITOR_HEADER = """<header class="site-header">
	<div class="wrap header-row">
		<a class="brand" href="../../index.html">
			<img src="../static/img/icon.svg" alt="" width="28" height="28">
			<span>open32<b>plus</b></span>
		</a>
		<nav id="site-nav" class="site-nav" aria-label="Site"><a href="../../index.html">Install</a> <a href="./" aria-current="page">Channels &amp; zones</a></nav>
		<div class="header-tools">
			<button class="theme-toggle" type="button" aria-label="Theme" title="Theme">&#9680;</button>
		</div>
	</div>
</header>"""
EDITOR_FOOTER = """<footer class="site-footer">
	<div class="wrap footer-row">
		<p>open32plus, based on <a href="https://github.com/radiofoundry/radiofoundry">RadioFoundry</a> &middot; MIT license &middot; <a href="%s">Source</a> &middot; <a href="%s/issues">Report a problem</a></p>
	</div>
</footer>"""


def redirect(path, to):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write('<!doctype html>\n<!-- SPDX-License-Identifier: MIT -->\n<meta charset="utf-8"><title>open32plus</title>'
                '<meta http-equiv="refresh" content="0; url=%s"><a href="%s">%s</a>\n' % (to, to, to))


def editor_page(html, repo_url):
    """The site's codeplug page as open32plus's: its own header and footer, no other languages (the page is English
    only here), the name"""
    html = re.sub(r'<header class="site-header">.*?</header>', lambda m: EDITOR_HEADER, html, count=1, flags=re.S)
    html = re.sub(r'<footer class="site-footer">.*?</footer>', lambda m: "@@FOOTER@@", html, count=1, flags=re.S)
    html = re.sub(r'<link rel="alternate"[^>]*>\s*', "", html)
    html = html.replace("RadioFoundry", "open32plus")
    return html.replace("@@FOOTER@@", EDITOR_FOOTER % (repo_url, repo_url))


def add_editor(site, out, repo_url=REPO_URL):
    """The codeplug editor from a built site (site/build.py --out SITE) under out/editor/: the page at
    editor/codeplug/, its own static/ and data/ (the installer's data/ is another format), and the pages it links to
    that this site doesn't have sent on (the flasher to the installer, the docs to the repository's)"""
    page = os.path.join(site, "codeplug", "index.html")
    if not os.path.isfile(page):
        sys.exit("installer_build: %s has no codeplug page: build the site first (site/build.py --out %s)" % (site, site))
    dst = os.path.join(out, EDITOR)
    with open(page, encoding="utf-8") as f:
        html = editor_page(f.read(), repo_url)
    os.makedirs(os.path.join(dst, "codeplug"))
    with open(os.path.join(dst, "codeplug", "index.html"), "w", encoding="utf-8") as f:
        f.write(html)
    for d in ("js", "css", "vendor"):
        shutil.copytree(os.path.join(site, "static", d), os.path.join(dst, "static", d))
    os.makedirs(os.path.join(dst, "static", "img"))
    for n in EDITOR_IMG:
        shutil.copy2(os.path.join(site, "static", "img", n), os.path.join(dst, "static", "img", n))
    os.makedirs(os.path.join(dst, "data"))
    for n in ("radios.json", "releases.json"):
        shutil.copy2(os.path.join(site, "data", n), os.path.join(dst, "data", n))
    # The site's settings, pointed here: no error reporting address, this repository for help
    with open(os.path.join(site, "data", "site.json"), encoding="utf-8") as f:
        info = json.load(f)
    info.pop("reports", None)
    info.update(repo=repo_url, community=repo_url + "/issues", languages=[l for l in info.get("languages", []) if l.get("code") == "en"])
    with open(os.path.join(dst, "data", "site.json"), "w", encoding="utf-8") as f:
        json.dump(info, f, indent=1)
    redirect(os.path.join(dst, "index.html"), "codeplug/")
    redirect(os.path.join(dst, "flash", "index.html"), "../../index.html")
    redirect(os.path.join(dst, "docs", "index.html"), repo_url + "/tree/main/docs/user")
    return dst


def radios():
    """The radios with the Baofeng serial updater: [{id, name, keys, sign}], from the board registry"""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import serial_loader
    return [{"id": m.id, "name": m.name, "keys": m.keys, "sign": m.sign}
            for m in sorted(serial_loader.MODELS.values(), key=lambda m: m.name)]


def c7000_data():
    sys.path.insert(0, os.path.join(ROOT, "site"))
    from rfoundrysite import repo
    return repo.flash_boards()["c7000"]


def release_files(fwdir, boards):
    """{board: (manifest dict, manifest name, image name)} of the release in fwdir, one per board in boards"""
    out = {}
    for name in sorted(os.listdir(fwdir)):
        if not (name.startswith("rfoundry-") and name.endswith(".json")):
            continue
        with open(os.path.join(fwdir, name), encoding="utf-8") as f:
            m = json.load(f)
        # the plain image only: a variant (rfoundry-<board>-<variant>-<version>) names its variant
        if m.get("board") not in boards or m.get("variant") not in (None, "", "default"):
            continue
        image = (m.get("image") or {}).get("file")
        if not image or not os.path.isfile(os.path.join(fwdir, image)):
            sys.exit("installer_build: %s names %s, which isn't in %s" % (name, image, fwdir))
        if m["board"] in out and out[m["board"]][0].get("version") != m.get("version"):
            sys.exit("installer_build: %s holds two releases for %s" % (fwdir, m["board"]))
        out[m["board"]] = (m, name, image)
    return out


def build(out, fwdir=None, commit=None, repo_url=REPO_URL, editor=None):
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "data"))
    for name in PAGE:
        shutil.copy2(os.path.join(SRC, name), os.path.join(out, name))
    mods = modules()
    for p in mods:
        dst = os.path.join(out, "js", os.path.relpath(p, JS))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(p, dst)
    c = c7000_data()
    rs = [r for r in radios() if c["boards"].get(r["id"], {}).get("bootloader_id")]
    with open(os.path.join(out, "data", "c7000.json"), "w", encoding="utf-8") as f:
        json.dump(c, f, separators=(",", ":"))
    with open(os.path.join(out, "data", "radios.json"), "w", encoding="utf-8") as f:
        json.dump(rs, f, indent=1)
    rel = release_files(fwdir, {r["id"] for r in rs}) if fwdir else {}
    if rel:
        os.makedirs(os.path.join(out, "firmware"))
        images = {}
        for board, (m, mname, iname) in sorted(rel.items()):
            for n in (mname, iname):
                shutil.copy2(os.path.join(fwdir, n), os.path.join(out, "firmware", n))
            images[board] = {"image": "firmware/" + iname, "manifest": "firmware/" + mname}
        m = next(iter(rel.values()))[0]
        version = str(m.get("version"))
        info = {"version": version, "date": str(m.get("date") or m.get("built") or "")[:10], "images": images,
                "notes": "%s/releases/tag/%s" % (repo_url, version)}
        with open(os.path.join(out, "data", "release.json"), "w", encoding="utf-8") as f:
            json.dump(info, f, indent=1)
    if commit is None:
        r = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        commit = r.stdout.strip() or "local"
    with open(os.path.join(out, "data", "site.json"), "w", encoding="utf-8") as f:
        json.dump({"commit": commit[:8]}, f)
    open(os.path.join(out, ".nojekyll"), "w").close()	# GitHub Pages: serve every file as it is
    if editor:
        add_editor(editor, out, repo_url)
    print("installer: %s, %d modules, %d radios, release %s%s" % (
        out, len(mods), len(rs), "%s for %s" % (info["version"], ", ".join(sorted(rel))) if rel else "none",
        ", codeplug editor" if editor else ""))
    return mods, rel


def self_test():
    import tempfile
    mods = modules()
    names = {os.path.relpath(p, JS) for p in mods}
    for need in ("flasher/bfloader.js", "flasher/c7000.js", "flasher/link.js", "flasher/iap.js", "diag.js"):
        assert need in names, "%s not followed (%s)" % (need, sorted(names))
    # nothing that reports or builds through the RadioFoundry service
    for p in mods:
        with open(p, encoding="utf-8") as f:
            text = f.read()
        assert "api.radiofoundry.org" not in text, "%s reaches the RadioFoundry service" % p
    rs = radios()
    loaders = [r["id"] for r in rs if c7000_data()["boards"].get(r["id"], {}).get("bootloader_id")]
    assert loaders, "no radio with the Baofeng updater in the registry"
    assert all(r["name"] and r["keys"] for r in rs), rs
    board, other = loaders[0], "not-a-loader-radio"
    with tempfile.TemporaryDirectory() as d:
        fw = os.path.join(d, "fw")
        os.makedirs(fw)
        img = b"\x01\x02" * 100
        import hashlib
        for b_ in (board, other):
            man = {"format": "rfoundry-release/1", "board": b_, "version": "v26.11",
                   "image": {"file": "rfoundry-%s-v26.11.bin" % b_, "size": len(img), "sha256": hashlib.sha256(img).hexdigest()}}
            with open(os.path.join(fw, "rfoundry-%s-v26.11.bin" % b_), "wb") as f:
                f.write(img)
            with open(os.path.join(fw, "rfoundry-%s-v26.11.json" % b_), "w") as f:
                json.dump(man, f)
        out = os.path.join(d, "out")
        build(out, fw, commit="abcdef12")
        info = json.load(open(os.path.join(out, "data", "release.json")))
        assert info["version"] == "v26.11" and info["images"][board]["image"] == "firmware/rfoundry-%s-v26.11.bin" % board, info
        assert other not in info["images"], "a radio without the updater is left out"
        assert info["notes"].endswith("/releases/tag/v26.11"), info
        assert os.path.isfile(os.path.join(out, "js", "flasher", "bfloader.js"))
        assert [r["id"] for r in json.load(open(os.path.join(out, "data", "radios.json")))] == loaders
        build(out, None, commit="abcdef12")
        assert not os.path.exists(os.path.join(out, "data", "release.json"))
        # the editor from a site build: open32plus's header and footer, its own data, no error reporting address
        site = os.path.join(d, "site")
        for sub in ("codeplug", "data", "static/js/editor", "static/css", "static/vendor", "static/img"):
            os.makedirs(os.path.join(site, sub), exist_ok=True)
        with open(os.path.join(site, "codeplug", "index.html"), "w") as f:
            f.write('<title>Codeplug · RadioFoundry</title><link rel="alternate" hreflang="es" href="../es/codeplug/">'
                    '<header class="site-header"><nav>Flash Docs</nav></header><main>RadioFoundry editor</main>'
                    '<footer class="site-footer">community</footer>')
        for n in EDITOR_IMG:
            open(os.path.join(site, "static", "img", n), "w").close()
        open(os.path.join(site, "static", "js", "editor", "app.js"), "w").close()
        for n, v in (("radios.json", {"radios": []}), ("releases.json", {"releases": []}),
                     ("site.json", {"repo": "x", "reports": {"sentry_dsn": "y"}, "languages": [{"code": "en"}, {"code": "es"}]})):
            with open(os.path.join(site, "data", n), "w") as f:
                json.dump(v, f)
        build(out, None, commit="abcdef12", editor=site)
        page = open(os.path.join(out, EDITOR, "codeplug", "index.html")).read()
        assert "open32<b>plus</b>" in page and "Channels &amp; zones" in page and "Flash Docs" not in page, page
        assert "community" not in page and 'rel="alternate"' not in page and "open32plus editor" in page, page
        assert "based on <a href" in page and "Codeplug · open32plus" in page, page
        info = json.load(open(os.path.join(out, EDITOR, "data", "site.json")))
        assert "reports" not in info and info["repo"] == REPO_URL and [l["code"] for l in info["languages"]] == ["en"], info
        for p_ in ("static/js/editor/app.js", "data/radios.json", "index.html", "flash/index.html", "docs/index.html"):
            assert os.path.isfile(os.path.join(out, EDITOR, p_)), p_
        assert os.path.isfile(os.path.join(out, "data", "radios.json")), "the installer's own data stays"
    print("installer_build: self-test ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "installer"))
    ap.add_argument("--firmware", help="a folder with a release's assets (rfoundry-<board>-<version>.bin and .json)")
    ap.add_argument("--commit", help="the commit the page is built from (default: HEAD)")
    ap.add_argument("--repo-url", default=REPO_URL)
    ap.add_argument("--editor", help="a built site (site/build.py --out DIR): adds its codeplug editor under editor/")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    build(a.out, a.firmware, a.commit, a.repo_url, a.editor)


if __name__ == "__main__":
    main()
