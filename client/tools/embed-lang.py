#!/usr/bin/env python3
"""Writes ui/lang/*.json into the LANG table in ui/index.html.

The page keeps its dictionaries inline so the client stays one file.
Translators edit the JSON; this puts it back. Run it, then
tools/embed-page.py. The English source string is the key: a language
with no entry for a string shows the English one.

    python3 tools/embed-lang.py
"""
import json
import pathlib

root = pathlib.Path(__file__).resolve().parent.parent
page = root / "ui" / "index.html"
html = page.read_text(encoding="utf-8")

BEGIN = "/* lang:begin -- written by tools/embed-lang.py from ui/lang/*.json */\n"
END = "/* lang:end */"

table = {}
for f in sorted((root / "ui" / "lang").glob("*.json")):
    table[f.stem] = json.loads(f.read_text(encoding="utf-8"))

body = ["const LANG = {\n"]
for code, words in table.items():
    body.append("  %s: {\n" % json.dumps(code))
    for en, translated in words.items():
        body.append("    %s: %s,\n" % (json.dumps(en, ensure_ascii=False),
                                       json.dumps(translated, ensure_ascii=False)))
    body.append("  },\n")
body.append("};\n")

start = html.index(BEGIN) + len(BEGIN)
end = html.index(END)
page.write_text(html[:start] + "".join(body) + html[end:], encoding="utf-8")
print("%d languages, %s" % (len(table), ", ".join("%s: %d" % (c, len(w)) for c, w in table.items())))
