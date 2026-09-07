#!/usr/bin/env python3
"""Builds docs/demo.html from ui/index.html and ui/demo.js.

The demo on the project page is the client's own page with a recorded
session in place of the client, so it has to be generated rather than
copied: the hand-made copy sat two releases behind before anyone
noticed. Run after changing the page.

    python3 tools/make-demo.py
"""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
page = (root / "ui" / "index.html").read_text(encoding="utf-8")
shim = (root / "ui" / "demo.js").read_text(encoding="utf-8")

version = re.search(r'#define XERABORA_VERSION "([^"]+)"', (root / "src" / "version.h").read_text())
shim = "const DEMO_VERSION = %r;\n" % (version.group(1) if version else "0.0.0") + shim

page = page.replace("<title>xerabora</title>", "<title>xeRAbora demo</title>", 1)

# The page hides account and network settings when it is opened from
# another machine, and the demo is served from a web host. Here every
# panel should show.
page, n = re.subn(r"const REMOTE = [^;]+;", "const REMOTE = false;", page, count=1)
if n != 1:
    raise SystemExit("REMOTE is not where make-demo.py expects it")

page = page.replace("<script>", "<script>\n" + shim + "</script>\n<script>", 1)
out = root.parent / "docs" / "demo.html"
out.write_text(page, encoding="utf-8")
print("wrote %s, %d bytes" % (out, len(page)))
