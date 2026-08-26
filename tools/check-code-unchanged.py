#!/usr/bin/env python3
"""Compare two C sources ignoring comments and whitespace.

Usage: check-code-unchanged.py [--allow-strings] ORIGINAL REWRITTEN

Exit 0 when the token streams match, 1 when they differ. Used during the
comment rewrite to prove that only comments changed. With --allow-strings
the contents of string literals are ignored too, so translated log
messages do not count as code changes.
"""
import re
import sys

ALLOW_STRINGS = False


def strip(src):
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            j = src.find('*/', i + 2)
            i = n if j < 0 else j + 2
            out.append(' ')
        elif c == '/' and i + 1 < n and src[i + 1] == '/':
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    text = ''.join(out)
    if ALLOW_STRINGS:
        text = re.sub(r'"(?:\\.|[^"\\])*"', '"S"', text)
    return re.findall(r'[A-Za-z_][A-Za-z0-9_]*|\d[\w.]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\S', text)


def main():
    global ALLOW_STRINGS
    args = [x for x in sys.argv[1:] if x != '--allow-strings']
    ALLOW_STRINGS = len(args) != len(sys.argv) - 1
    a, b = args[0], args[1]
    ta = strip(open(a, encoding='utf-8', errors='replace').read())
    tb = strip(open(b, encoding='utf-8', errors='replace').read())
    if ta == tb:
        print(f'OK   {b}')
        return 0
    for k, (x, y) in enumerate(zip(ta, tb)):
        if x != y:
            print(f'DIFF {b}: token {k}: {x!r} -> {y!r}')
            print('  original context :', ' '.join(ta[max(0, k - 8):k + 8]))
            print('  rewritten context:', ' '.join(tb[max(0, k - 8):k + 8]))
            return 1
    print(f'DIFF {b}: length {len(ta)} -> {len(tb)}')
    return 1


if __name__ == '__main__':
    sys.exit(main())
