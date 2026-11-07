#!/usr/bin/env python3
"""Assembles ui/index.html from the pieces in ui/src, then embeds it.

The page ships as one HTML file inside the executable, but it is
written as many: a skeleton, one stylesheet per concern, one script per
tab, the vendored libraries and the dictionaries. This puts them
together, in name order, and then runs the two steps that follow a page
change: tools/embed-page.py (src/ui_page.c) and tools/make-demo.py
(docs/demo.html).

    python3 tools/build-page.py            build once
    python3 tools/build-page.py --watch    rebuild whenever a piece changes
    python3 tools/build-page.py --page     the HTML only, no embed, no demo

Layout of ui/src:

    page.html      the skeleton, with {{fonts}}, {{css}}, {{vendor}}, {{lang}}, {{js}}
    css/*.css      concatenated in name order into one <style>
    js/*.js        concatenated in name order into one <script>
    ../vendor/     preact, hooks, htm, signals: wrapped as CommonJS modules
                   behind a tiny require(), so the CJS/UMD builds work inline
    ../vendor/fonts/ fonts.css and the woff2 cuts it names, inlined as data URIs
    ../lang/*.json the dictionaries, generated into the LANG table

Every file in css/ and js/ goes in; the leading number in a name fixes
the order where it matters (tokens before rules, app.js last).
"""
import base64
import json
import pathlib
import re
import runpy
import sys
import time

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "ui" / "src"
vendor = root / "ui" / "vendor"
langdir = root / "ui" / "lang"
out = root / "ui" / "index.html"

# Module name, file. Order matters: a module's dependencies come first.
VENDOR = [
    ("preact", "preact.umd.js"),
    ("preact/hooks", "hooks.umd.js"),
    ("htm", "htm.umd.js"),
    ("@preact/signals-core", "signals-core.js"),
    ("@preact/signals", "signals.js"),
]

REQUIRE = """\
/* A CommonJS corner for the vendored libraries: each is wrapped as a
   module and fetched through require(), the way their builds expect. */
var require = (function () {
  var mods = {};
  function require(name) {
    if (!(name in mods)) throw new Error('vendor module missing: ' + name);
    return mods[name];
  }
  require.define = function (name, body) {
    var module = { exports: {} };
    body(module.exports, require, module);
    mods[name] = module.exports;
  };
  return require;
})();
"""


def concat(folder, suffix, comment):
    parts = []
    for f in sorted(folder.glob("*" + suffix)):
        parts.append(comment % f.name + "\n" + f.read_text(encoding="utf-8").rstrip() + "\n")
    return "\n".join(parts)


def vendor_block():
    parts = [REQUIRE]
    for name, fname in VENDOR:
        code = (vendor / fname).read_text(encoding="utf-8").strip()
        parts.append("require.define(%s, function (exports, require, module) {\n%s\n});"
                     % (json.dumps(name), code))
    return "\n".join(parts) + "\n"


def fonts_block():
    """fonts.css with each url(file) replaced by that file as a data URI."""
    folder = vendor / "fonts"

    def inline(m):
        data = base64.b64encode((folder / m.group(1)).read_bytes()).decode("ascii")
        return "url(data:font/woff2;base64,%s)" % data

    return re.sub(r"url\(([^)]+)\)", inline, (folder / "fonts.css").read_text(encoding="utf-8"))


def lang_block():
    """The English source string is the key; a language with no entry for
    a string shows the English one."""
    table = {}
    for f in sorted(langdir.glob("*.json")):
        table[f.stem] = json.loads(f.read_text(encoding="utf-8"))
    body = ["/* Generated from ui/lang/*.json by tools/build-page.py. */",
            "const LANG = {"]
    for code, words in table.items():
        body.append("  %s: {" % json.dumps(code))
        for en, tr in words.items():
            body.append("    %s: %s," % (json.dumps(en, ensure_ascii=False),
                                         json.dumps(tr, ensure_ascii=False)))
        body.append("  },")
    body.append("};")
    return "\n".join(body) + "\n", {c: len(w) for c, w in table.items()}


def build_page():
    page = (src / "page.html").read_text(encoding="utf-8")
    css = concat(src / "css", ".css", "/* ==== %s ==== */")
    js = concat(src / "js", ".js", "/* ==== %s ==== */")
    lang, counts = lang_block()
    for marker, body in (("{{fonts}}", fonts_block()), ("{{css}}", css),
                         ("{{vendor}}", vendor_block()),
                         ("{{lang}}", lang), ("{{js}}", js)):
        if marker not in page:
            raise SystemExit("page.html has no %s" % marker)
        page = page.replace(marker, body, 1)
    out.write_text(page, encoding="utf-8")
    print("wrote %s, %d bytes; languages: %s" %
          (out.relative_to(root), len(page),
           ", ".join("%s %d" % kv for kv in counts.items())))


def build(page_only=False):
    build_page()
    if page_only:
        return
    runpy.run_path(str(root / "tools" / "embed-page.py"))
    runpy.run_path(str(root / "tools" / "make-demo.py"))


def inputs():
    files = [src / "page.html"] + list((src / "css").glob("*.css")) + \
            list((src / "js").glob("*.js")) + list(vendor.glob("*.js")) + \
            list((vendor / "fonts").iterdir()) + \
            list(langdir.glob("*.json")) + [root / "ui" / "demo.js"]
    return {f: f.stat().st_mtime for f in files}


def watch(page_only):
    seen = inputs()
    build(page_only)
    print("watching ui/src, ui/vendor, ui/lang, ui/demo.js (Ctrl-C stops)")
    while True:
        time.sleep(0.5)
        now = inputs()
        if now != seen:
            seen = now
            try:
                build(page_only)
            except Exception as e:  # keep watching after a broken edit
                print("build failed: %s" % e)


if __name__ == "__main__":
    page_only = "--page" in sys.argv
    if "--watch" in sys.argv:
        try:
            watch(page_only)
        except KeyboardInterrupt:
            pass
    else:
        build(page_only)
