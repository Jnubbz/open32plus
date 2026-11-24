#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Copyright (c) 2026 Josh Noble (KN0BLE)
"""Assemble the open32plus web installer (installer/README.md) into one static folder.
  tools/installer_build.py --out build/installer [--firmware DIR] [--commit SHA]
The page (installer/), the web flasher modules it imports (site/static/js/, followed import by import, so the copy
holds what runs and nothing else), the radio data (data/c7000.json, the site's flash-boards.json "c7000") and, with
--firmware, the newest DM-32 release image and manifest in DIR (a release's assets: rfoundry-dm32-<version>.bin and
.json), described in data/release.json. Without one the page installs only a file the user picks.
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
BOARD = "dm32"
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


def c7000_data():
    sys.path.insert(0, os.path.join(ROOT, "site"))
    from rfoundrysite import repo
    return repo.flash_boards()["c7000"]


def release_files(fwdir):
    """(manifest dict, manifest name, image name) of the newest DM-32 release in fwdir, or None"""
    best = None
    for name in sorted(os.listdir(fwdir)):
        if not (name.startswith("rfoundry-%s-" % BOARD) and name.endswith(".json")):
            continue
        with open(os.path.join(fwdir, name), encoding="utf-8") as f:
            m = json.load(f)
        # the plain image only: a variant (rfoundry-dm32-<variant>-<version>) names its variant
        if m.get("board") != BOARD or m.get("variant") not in (None, "", "default"):
            continue
        image = (m.get("image") or {}).get("file")
        if not image or not os.path.isfile(os.path.join(fwdir, image)):
            sys.exit("installer_build: %s names %s, which isn't in %s" % (name, image, fwdir))
        if best is None or str(m.get("version")) > str(best[0].get("version")):
            best = (m, name, image)
    return best


def build(out, fwdir=None, commit=None, repo_url=REPO_URL):
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
    with open(os.path.join(out, "data", "c7000.json"), "w", encoding="utf-8") as f:
        json.dump(c7000_data(), f, separators=(",", ":"))
    rel = release_files(fwdir) if fwdir else None
    if rel:
        m, mname, iname = rel
        os.makedirs(os.path.join(out, "firmware"))
        for n in (mname, iname):
            shutil.copy2(os.path.join(fwdir, n), os.path.join(out, "firmware", n))
        version = str(m.get("version"))
        info = {"version": version, "date": str(m.get("date") or m.get("built") or "")[:10],
                "image": "firmware/" + iname, "manifest": "firmware/" + mname,
                "notes": "%s/releases/tag/%s" % (repo_url, version)}
        with open(os.path.join(out, "data", "release.json"), "w", encoding="utf-8") as f:
            json.dump(info, f, indent=1)
    if commit is None:
        r = subprocess.run(["git", "rev-parse", "--short=8", "HEAD"], cwd=ROOT, capture_output=True, text=True)
        commit = r.stdout.strip() or "local"
    with open(os.path.join(out, "data", "site.json"), "w", encoding="utf-8") as f:
        json.dump({"commit": commit[:8]}, f)
    open(os.path.join(out, ".nojekyll"), "w").close()	# GitHub Pages: serve every file as it is
    print("installer: %s, %d modules, release %s" % (out, len(mods), rel[0].get("version") if rel else "none"))
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
    with tempfile.TemporaryDirectory() as d:
        fw = os.path.join(d, "fw")
        os.makedirs(fw)
        img = b"\x01\x02" * 100
        import hashlib
        man = {"format": "rfoundry-release/1", "board": BOARD, "version": "v26.11", "image": {"file": "rfoundry-dm32-v26.11.bin", "size": len(img), "sha256": hashlib.sha256(img).hexdigest()}}
        with open(os.path.join(fw, "rfoundry-dm32-v26.11.bin"), "wb") as f:
            f.write(img)
        with open(os.path.join(fw, "rfoundry-dm32-v26.11.json"), "w") as f:
            json.dump(man, f)
        with open(os.path.join(fw, "rfoundry-rt10-v26.11.json"), "w") as f:
            json.dump(dict(man, board="rt10"), f)
        out = os.path.join(d, "out")
        build(out, fw, commit="abcdef12")
        info = json.load(open(os.path.join(out, "data", "release.json")))
        assert info["version"] == "v26.11" and info["image"] == "firmware/rfoundry-dm32-v26.11.bin", info
        assert info["notes"].endswith("/releases/tag/v26.11"), info
        assert os.path.isfile(os.path.join(out, "js", "flasher", "bfloader.js"))
        assert BOARD in json.load(open(os.path.join(out, "data", "c7000.json")))["boards"]
        build(out, None, commit="abcdef12")
        assert not os.path.exists(os.path.join(out, "data", "release.json"))
    print("installer_build: self-test ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "installer"))
    ap.add_argument("--firmware", help="a folder with a release's assets (rfoundry-dm32-<version>.bin and .json)")
    ap.add_argument("--commit", help="the commit the page is built from (default: HEAD)")
    ap.add_argument("--repo-url", default=REPO_URL)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    build(a.out, a.firmware, a.commit, a.repo_url)


if __name__ == "__main__":
    main()
