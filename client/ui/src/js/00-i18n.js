/* ---- Language --------------------------------------------------------- */

/* t() looks a string up in LANG[lang] and falls back to the English
   key. LANG is generated from ui/lang/*.json at build time. {name}
   placeholders are filled after the lookup. */
const LANGS = [['', 'English'], ['pt-BR', 'Português (Brasil)'], ['es', 'Español'], ['ru', 'Русский']];

let lang = '';
try { lang = localStorage.getItem('lang') || ''; } catch (e) { lang = ''; }
/* lang= in the query string or the hash overrides the stored choice
   for this window. */
{
  const q = new URLSearchParams(location.search).get('lang') ||
            new URLSearchParams(location.hash.slice(1)).get('lang');

  if (q !== null && LANGS.some(([code]) => code === q)) lang = q;
}

/* Keys and translations may carry &middot;, &mdash; and &amp;. The
   page renders text nodes, so they become the characters. */
function entities(s) {
  return s.replace(/&middot;/g, '·').replace(/&mdash;/g, '—').replace(/&amp;/g, '&');
}

function t(s, v) {
  const d = LANG[lang];
  let out = entities((d && d[s]) || s);

  if (v) for (const k in v) out = out.split('{' + k + '}').join(String(v[k]));
  return out;
}

/* t() with elements among the values: the string is split on each
   placeholder and returned as an array of strings and nodes. */
function tv(s, v) {
  let parts = [t(s)];

  for (const k in v) {
    const next = [];

    for (const p of parts) {
      if (typeof p !== 'string') { next.push(p); continue; }
      p.split('{' + k + '}').forEach((bit, i) => {
        if (i) next.push(v[k]);
        if (bit) next.push(bit);
      });
    }
    parts = next;
  }
  return parts;
}

/* Stores the choice in this browser and reloads. */
function setLang(code) {
  try { localStorage.setItem('lang', code); } catch (e) { /* private window */ }
  location.reload();
}
