#!/usr/bin/env python3
"""Builds the translated project pages from docs/index.html.

    python3 tools/translate-page.py --dump      # write docs/lang/en.json
    python3 tools/translate-page.py             # write docs/pt-br.html, docs/es.html

English is the source. A unit is one leaf block of the page (a
paragraph, a heading, a list item, a caption) taken with the markup
inside it, so a sentence wrapped around <code> or <a> stays one string
to translate. The English text is the key: a missing entry leaves that
piece in English, so a half-finished translation still builds.

The translated pages sit next to index.html rather than in folders, so
every relative link and the demo iframe keep working.
"""
import html.parser
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "index.html"
LANGDIR = ROOT / "docs" / "lang"

BLOCK = {"h1", "h2", "h3", "h4", "p", "li", "button", "figcaption", "summary",
         "td", "th", "label", "dt", "dd"}
EXTRA = re.compile(r"\b(tip|cap|note|lead|caption)\b")
VOID = {"br", "img", "input", "meta", "link", "hr", "source"}
SKIP_CLASS = re.compile(r"\blangs\b")

PAGES = [("pt-BR", "pt-br.html"), ("es", "es.html")]


class Units(html.parser.HTMLParser):
    """Collects (start, end, text) for every translatable piece."""

    def __init__(self, raw, mode):
        super().__init__(convert_charrefs=False)
        self.raw = raw
        self.mode = mode
        self.starts = [0]
        for line in raw.split("\n")[:-1]:
            self.starts.append(self.starts[-1] + len(line) + 1)
        self.stack = []
        self.skip = 0
        self.units = []
        self.covered = []

    def off(self):
        line, col = self.getpos()
        return self.starts[line - 1] + col

    def inside_skipped(self):
        return any(SKIP_CLASS.search(f[3]) for f in self.stack)

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style"):
            self.skip += 1
            return
        if self.skip:
            return
        d = dict(attrs)
        for key in ("alt", "title", "aria-label") if self.mode == "blocks" else ():
            if d.get(key) and re.search(r"[A-Za-z]{3}", d[key]) and not self.inside_skipped():
                at = self.off() + self.get_starttag_text().index(d[key])
                self.units.append((at, at + len(d[key]), d[key]))
        if tag in VOID:
            return
        parent = self.stack[-1][3] if self.stack else ""
        self.stack.append((tag, self.off() + len(self.get_starttag_text()),
                           self.getpos()[0], d.get("class", ""), parent))

    def handle_endtag(self, tag):
        if tag in ("script", "style"):
            self.skip = max(0, self.skip - 1)
            return
        if self.skip:
            return
        skipped = self.inside_skipped()
        frame = None
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i][0] == tag:
                frame = self.stack[i]
                del self.stack[i:]
                break
        if frame is None or skipped:
            return
        cls, parent = frame[3], frame[4]
        if not (tag in BLOCK or EXTRA.search(cls) or (tag == "span" and "hero-meta" in parent)):
            return
        end = self.off()
        inner = self.raw[frame[1]:end]
        text = " ".join(inner.strip().split())
        if not re.search(r"[A-Za-z]{2}", re.sub(r"<[^>]*>", "", text)):
            return
        if re.search(r"<(h[1-4]|p|li|button|table|section|div)\b", text):
            return
        self.units.append((frame[1], end, text))
        self.covered.append((frame[1], end))

    def handle_data(self, data):
        if self.mode != "loose" or self.skip or self.inside_skipped():
            return
        text = " ".join(data.split())
        if not text or not re.search(r"[A-Za-z]{2}", text):
            return
        at = self.off()
        if any(a <= at < b for a, b in self.covered):
            return
        lead = len(data) - len(data.lstrip())
        self.units.append((at + lead, at + lead + len(data.strip()), text))


SKIP_TEXT = {"xe", "bora"}


def collect(raw):
    """Blocks first, so their ranges are known; then the text between them."""
    blocks = Units(raw, "blocks")
    blocks.feed(raw)
    loose = Units(raw, "loose")
    loose.covered = blocks.covered
    loose.feed(raw)
    seen = {}
    for start, end, text in blocks.units + loose.units:
        if text not in SKIP_TEXT:
            seen[(start, end)] = text
    return sorted((s, e, t) for (s, e), t in seen.items())


def build(raw, code, filename, words):
    units = collect(raw)
    out = raw
    for start, end, text in sorted(units, reverse=True):
        if text in words and words[text]:
            out = out[:start] + words[text] + out[end:]

    out = out.replace('<html lang="en">', '<html lang="%s">' % code, 1)
    out = out.replace('<link rel="canonical" href="https://hacan359.github.io/xerabora/">',
                      '<link rel="canonical" href="https://hacan359.github.io/xerabora/%s">' % filename, 1)
    out = out.replace('<a href="./" class="on">EN</a>', '<a href="./">EN</a>', 1)
    tag = code.split("-")[0].upper()
    out = out.replace('<a href="%s">%s</a>' % (filename, tag),
                      '<a href="%s" class="on">%s</a>' % (filename, tag), 1)
    # the demo is the client's page: open it in the same language
    out = out.replace('src="demo.html"', 'src="demo.html#lang=%s"' % code, 1)
    return out


def main():
    raw = SRC.read_text(encoding="utf-8")
    LANGDIR.mkdir(exist_ok=True)

    if "--dump" in sys.argv:
        table = {}
        for _, _, text in collect(raw):
            table.setdefault(text, "")
        (LANGDIR / "en.json").write_text(
            json.dumps(table, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("%d strings -> docs/lang/en.json" % len(table))
        return

    for code, filename in PAGES:
        f = LANGDIR / (code + ".json")
        if not f.exists():
            print("no %s, skipped" % f)
            continue
        words = json.loads(f.read_text(encoding="utf-8"))
        strings = {text for _, _, text in collect(raw)}
        done = sum(1 for text in strings if words.get(text))
        total = len(strings)
        (ROOT / "docs" / filename).write_text(build(raw, code, filename, words), encoding="utf-8")
        print("docs/%s: %d of %d strings translated" % (filename, done, total))


main()
