/* ---- Small helpers, shared by every tab -------------------------------- */

const BADGE = 'https://media.retroachievements.org/Badge/';
const MEDIA = 'https://media.retroachievements.org';

/* True when the page was opened from another device. Settings, the
   follow switch and QUIT are then hidden. */
const REMOTE = !['127.0.0.1', 'localhost', '::1', '[::1]'].includes(location.hostname);

const TABS = ['live', 'library', 'game', 'boards', 'settings'];

/* The looks, chosen on SETTINGS and kept in this browser. */
const THEMES = [['neon', 'NEON CLASSIC'], ['8bit', '8-BIT']];

/* The page's views, one per address: the tabs at /, the stream screen
   at /stream. */
const VIEWS = ['tabs', 'stream'];
function viewFromPath(path) {
  /* The view from the address: the last part of the path, with or
     without .html. */
  const p = (path || '').replace(/\/+$/, '').split('/').pop().replace(/\.html?$/, '');
  return VIEWS.includes(p) ? p : 'tabs';
}

function hms(s) {
  const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60);
  return h + ':' + String(m).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
}

function pct(a, b) { return b ? Math.round(a * 100 / b) : 0; }

/* rc_client's measured_percent is already 0..100. */
function pctClamp(v) { return Math.max(0, Math.min(100, +v || 0)); }

function ago(sec) {
  if (sec < 60) return t('{n} s', { n: sec });
  if (sec < 3600) return t('{n} min', { n: Math.floor(sec / 60) });
  return t('{h} h {m} min', { h: Math.floor(sec / 3600), m: Math.floor((sec % 3600) / 60) });
}

/* The whole line is one dictionary string. */
function agoText(sec) { return t('{time} ago', { time: ago(sec) }); }

function medianText(s) {
  return Math.floor(s / 3600) + 'h' + String(Math.floor(s % 3600 / 60)).padStart(2, '0');
}

function badgeUrl(a, locked) { return BADGE + a.badge + (locked ? '_lock' : '') + '.png'; }

/* Rank in the progression-path order; untyped rows take 2. */
const TYPE_RANK = { progression: 0, win_condition: 1, missable: 3 };

/* achType() maps rc_client's numeric type or the Web API's string to
   'missable', 'progression', 'win_condition' or ''. */
const TYPE_NAMES = ['', 'missable', 'progression', 'win_condition'];

function achType(a) {
  return typeof a.type === 'number' ? (TYPE_NAMES[a.type] || '')
                                    : (a.type || '').toLowerCase();
}

function onPath(a) {
  const ty = achType(a);
  return ty === 'progression' || ty === 'win_condition';
}

/* Time cost of a set from its medians: beaten is the latest median on
   the intended path, full the latest of all. */
function setEstimates(list, medOf) {
  const meds = list.map(medOf).filter(m => m > 0);
  const path = list.filter(onPath).map(medOf).filter(m => m > 0);

  return { full: meds.length ? Math.max(...meds) : 0,
           beaten: path.length ? Math.max(...path) : 0 };
}

/* UP NEXT: the four locked achievements with the lowest medians. The
   first locked progression or win-condition achievement takes the last
   slot when it is not among them. */
function upNextPick(list, med) {
  const byMed = (x, y) => med(x) - med(y);
  const locked = list.filter(a => a.state === 1 && med(a) > 0);
  const spine = locked.filter(onPath).sort(byMed);
  const next = locked.sort(byMed).slice(0, 4);

  if (spine.length && !next.includes(spine[0])) {
    next[next.length - 1] = spine[0];
    next.sort(byMed);
  }
  return { next, prog: spine.length ? spine[0] : null };
}

/* Locked missables whose median is at or before the latest median
   among the unlocked progression and win-condition achievements. */
function possiblyMissed(list, med) {
  const passed = Math.max(0, ...list.filter(a => a.state === 2 && onPath(a)).map(med));
  const out = new Set();

  for (const a of list)
    if (a.state === 1 && achType(a) === 'missable' && med(a) > 0 && med(a) <= passed)
      out.add(a.id);
  return out;
}

/* Sorts achievements by author, type, median or rarity; `get(a)`
   returns a row's order, median and players. */
function sortAchievements(list, order, get) {
  const g = get || (a => a);
  const byAuthor = (a, b) => (g(a).order || 0) - (g(b).order || 0);

  if (order === 'median')
    /* Never-unlocked-by-anyone (median 0) goes to the end. */
    return list.sort((a, b) =>
      (g(a).median > 0 ? g(a).median : Infinity) - (g(b).median > 0 ? g(b).median : Infinity) || byAuthor(a, b));
  if (order === 'rarity')
    /* Rarest first; unknown rarity last. */
    return list.sort((a, b) =>
      (g(a).players > 0 ? g(a).players : Infinity) - (g(b).players > 0 ? g(b).players : Infinity) || byAuthor(a, b));
  if (order === 'type')
    return list.sort((a, b) =>
      (TYPE_RANK[achType(a)] ?? 2) - (TYPE_RANK[achType(b)] ?? 2) || byAuthor(a, b));
  return list.sort(byAuthor);
}

/* Filter, order and view choices: key, label, icon, tooltip text. */
const ACH_FILTERS = [
  ['all', 'ALL', 'all', 'every achievement of the set'],
  ['locked', 'LOCKED', 'lock', 'still to unlock'],
  ['unlocked', 'UNLOCKED', 'unlock', 'already earned'],
  ['missable', 'MISSABLE', 'warn', 'can be lost for the rest of the playthrough'],
  ['progression', 'PROGRESSION', 'flag', 'on the path through the game'],
  ['win_condition', 'WIN CONDITION', 'trophy', 'beats the game']];
const ACH_SORTS = [
  ['author', 'AUTHOR ORDER', 'list-num', 'the order the set author gave'],
  ['type', 'PROGRESSION PATH', 'route', 'progression first, then win conditions, then the rest'],
  ['median', 'MEDIAN TIME', 'clock', 'the order players unlock them in, by median time'],
  ['rarity', 'RARITY', 'gem', 'rarest first, by the share of players who have it']];
const ACH_VIEWS = [
  ['list', 'LIST', 'rows', 'one row per achievement, with the words'],
  ['grid', 'GRID', 'grid', 'badges only, the way the site shows a set']];

/* Keeps the rows that match the filter; `done(a)` tells an unlocked row. */
function filterAchievements(list, filter, done) {
  return list.filter(a =>
    filter === 'all' ? true :
    filter === 'locked' ? !done(a) :
    filter === 'unlocked' ? done(a) :
    achType(a) === filter);
}
