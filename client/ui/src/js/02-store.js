/* ---- The store: signals the components read ---------------------------- */

const { h, render, Fragment } = require('preact');
const { useState, useEffect, useRef, useMemo } = require('preact/hooks');
const { signal, computed, effect, batch } = require('@preact/signals');
const html = require('htm').bind(h);

/* Slow state: a full /state once a second and on anything structural,
   what the user clicked, what the tabs fetched. */
const S = {
  state: signal(null),          /* the last full state from the client */
  profile: signal(null),        /* /profile: name and points */
  version: signal(''),
  tab: signal('live'),
  gameId: signal(0),            /* the game open on GAME and BOARDS */
  library: signal(null),
  libraryAt: 0,
  libFilter: signal('all'),
  libConsoles: signal(new Set()),   /* empty = every console */
  libConsolesOpen: signal(false),
  gameSort: signal('author'),
  gameFilter: signal('all'),
  gameCache: {},                /* id -> /game answer */
  gameTick: signal(0),          /* bumped when the cache changes */
  boardsCache: {},
  boardsTick: signal(0),
  openBoards: signal(new Set()),
  liveMeta: signal({ id: 0, byId: null, icon: '', game: null }),  /* /game for the running game: medians, rarity, rank */
  view: signal(viewFromPath(location.pathname)),   /* 'tabs' or 'stream' */
  toast: signal(null),
  powerOn: signal(0),           /* bumped when the console starts streaming */
  closed: signal(false),
  error: signal(''),
  liveSub: signal(loadPref('liveSub', 'ach')),   /* LIVE: 'ach', 'lb' or 'src' */
  achView: signal(loadPref('achView', 'list')),  /* 'list' or 'grid' */
  theme: signal(new URLSearchParams(location.hash.slice(1)).get('theme') || loadPref('theme', 'neon')),  /* 'neon' or '8bit'; #theme= in the address sets it for this window */
  upNext: signal(loadPref('upNext', 'open')),    /* 'open' or 'shut' */
  liveFilter: signal('all'),
  liveSort: signal(loadPref('liveSort', 'author')),
  openAch: signal(0),           /* the achievement whose comments are open */
  commentsCache: {},            /* id -> /comments answer */
  commentsTick: signal(0),
  hidden: signal(loadHidden())  /* game id -> achievement ids kept out of UP NEXT */
};

/* Small per-browser choices: the LIVE sub-tab and the list or grid view. */
function loadPref(key, def) {
  try { return localStorage.getItem(key) || def; } catch (e) { return def; }
}

function setPref(key, val) {
  S[key].value = val;
  try { localStorage.setItem(key, val); } catch (e) { /* private window */ }
}


/* Hidden achievements live in this browser's localStorage under 'hidden'. */
function loadHidden() {
  try { return JSON.parse(localStorage.getItem('hidden') || '{}') || {}; } catch (e) { return {}; }
}

function setHidden(gameId, ids) {
  const h = Object.assign({}, S.hidden.value);
  if (ids.length) h[gameId] = ids; else delete h[gameId];
  S.hidden.value = h;
  try { localStorage.setItem('hidden', JSON.stringify(h)); } catch (e) { /* private window */ }
}

function hideAch(gameId, id) {
  const cur = S.hidden.value[gameId] || [];
  if (!cur.includes(id)) setHidden(gameId, cur.concat(id));
}

function showHidden(gameId) { setHidden(gameId, []); }

/* Switches the view and puts it in the address bar. */
function go(view) {
  S.view.value = view;
  const path = view === 'tabs' ? '/' : '/' + view;
  try { history.pushState(null, '', path + location.search + location.hash); } catch (e) { /* no history on a file: page */ }
}

/* Fast state: the numbers a delta carries every snapshot, one signal
   each. */
const fast = {
  frames: signal(0), packets: signal(0), gaps: signal(0), dupes: signal(0), torn: signal(0),
  seconds: signal(0), rate: signal(0), loss: signal(0),
  fSeen: signal(-1),
  hw: signal({})                /* the console's own counters, see write_hw */
};

/* Measured progress per achievement and the value per tracked
   leaderboard: one signal per id, created on first use. */
const liveVals = new Map();
function liveVal(id) {
  if (!liveVals.has(id)) liveVals.set(id, signal({ m: '', p: 0 }));
  return liveVals.get(id);
}
const trackVals = new Map();
function trackVal(id) {
  if (!trackVals.has(id)) trackVals.set(id, signal(''));
  return trackVals.get(id);
}

/* What the page is listening to. The console has LIVE while it
   streams; following takes over when it stops; otherwise idle. */
const mode = computed(() => {
  const s = S.state.value;
  if (!s) return 'idle';
  return s.console.connected ? 'console' : (s.follow || {}).active ? 'follow' : 'idle';
});

/* Snapshots per second, from the frame counter's own movement, and
   the share of packets lost over the last few seconds. */
const rateWin = { frames: 0, at: 0 };
const lossWin = [];
function measure(c) {
  const now = performance.now();
  const frames = c.frames || 0, gaps = c.gaps || 0;

  if (!rateWin.at || frames < rateWin.frames) { rateWin.frames = frames; rateWin.at = now; fast.rate.value = 0; }
  else if (now - rateWin.at >= 1000) {
    fast.rate.value = Math.round((frames - rateWin.frames) * 1000 / (now - rateWin.at));
    rateWin.frames = frames; rateWin.at = now;
  }

  lossWin.push({ at: now, frames, gaps });
  while (lossWin.length && now - lossWin[0].at > 3000) lossWin.shift();
  const a = lossWin[0], b = lossWin[lossWin.length - 1];
  const df = b.frames - a.frames, dg = b.gaps - a.gaps;
  fast.loss.value = df + dg > 0 && dg >= 0 ? Math.min(1, dg / (df + dg)) : 0;
}

function setFast(c) {
  fast.frames.value = c.frames || 0;
  fast.packets.value = c.packets || 0;
  fast.gaps.value = c.gaps || 0;
  fast.dupes.value = c.dupes || 0;
  fast.torn.value = c.torn || 0;
  fast.seconds.value = c.seconds || 0;
  if (c.hw) fast.hw.value = c.hw;
  measure(c);
}

function setLive(list, trk) {
  for (const a of list || []) {
    const sig = liveVal(a.id), cur = sig.value;
    const m = a.m != null ? a.m : (a.measured || ''), p = pctClamp(a.p != null ? a.p : a.percent);
    if (cur.m !== m || cur.p !== p) sig.value = { m, p };
  }
  for (const tr of trk || []) {
    const v = tr.v != null ? tr.v : tr.value;
    const sig = trackVal(tr.id);
    if (sig.value !== v) sig.value = v;
  }
}

let lastUnlock = 0, wasConsole = false;

/* Applies a full state: the counters, the live values, the follow
   clocks, the state signal, the power-on trigger and the unlock toast. */
function applyState(s) {
  if (!s.game) s.game = { achievements: [] };
  batch(() => {
    const c = s.console || {};
    const f = s.follow || {};

    if (s.version) S.version.value = s.version;
    setFast(c);
    setLive(s.game.achievements, s.game.tracking);
    if (f.active) {
      fast.fSeen.value = f.rp_ago >= 0 ? f.rp_ago : -1;
    }
    S.state.value = s;

    /* connected went from false to true. */
    if (c.connected && !wasConsole) S.powerOn.value++;
    wasConsole = !!c.connected;

    const u = s.unlocks && s.unlocks[0];
    if (u && u.id !== lastUnlock && u.ago < 12) {
      lastUnlock = u.id;
      delete S.gameCache[s.game.id];
      S.gameTick.value++;
      /* The library is dropped and fetched again on the next visit. */
      S.library.value = null;
      S.toast.value = { id: u.id, title: u.title, badge: u.badge || '', points: u.points };
      refreshLiveMeta(s.game.id);
      setTimeout(() => { if (S.toast.value && S.toast.value.id === u.id) S.toast.value = null; }, 6000);
    }
  });
  if (s.game.id && S.liveMeta.value.id !== s.game.id) loadLiveMeta(s.game.id);
}

/* Fetches /game again 35 s after an unlock, so rarity and rank update. */
function refreshLiveMeta(id) {
  setTimeout(() => { if (S.liveMeta.value.id === id) loadLiveMeta(id); }, 35000);
}

function applyDelta(d) {
  batch(() => {
    if (d.console) setFast(d.console);
    setLive(d.live, d.tracking);
  });
}
