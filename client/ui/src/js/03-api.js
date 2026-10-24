/* ---- The client's endpoints -------------------------------------------- */

async function getJSON(url) {
  return (await fetch(url, { cache: 'no-store' })).json();
}

async function postForm(url, data) {
  const r = await fetch(url, { method: 'POST', body: new URLSearchParams(data) });
  return r.json();
}

async function refreshState() {
  applyState(await getJSON('/state'));
}

/* /events: one full state or one delta per message. The browser
   reconnects on its own; tick() polls /state every 2 s as well. */
function stream() {
  const es = new EventSource('/events');
  es.onmessage = e => {
    try {
      const d = JSON.parse(e.data);
      if (d.delta) applyDelta(d); else applyState(d);
    } catch (err) { /* not JSON: skipped */ }
  };
}

async function tick() {
  try { await refreshState(); } catch (e) { /* keep polling */ }
  setTimeout(tick, 2000);
}

function loadProfile() {
  getJSON('/profile').then(p => { S.profile.value = p; }).catch(() => {});
}

/* Fetches /library unless a copy younger than two minutes is held. */
let libraryLoading = false;
async function loadLibrary() {
  if (libraryLoading) return;
  if (S.library.value && Date.now() - S.libraryAt < 120000) return;
  libraryLoading = true;
  try {
    S.library.value = (await getJSON('/library')).games || [];
    S.libraryAt = Date.now();
  } catch (e) {
    S.library.value = { error: true };
  }
  libraryLoading = false;
}

/* Fetches /game once per id into S.gameCache and bumps gameTick. */
const gameLoading = new Set();
async function loadGame(id) {
  if (!id || S.gameCache[id] || gameLoading.has(id)) return;
  gameLoading.add(id);
  try {
    S.gameCache[id] = await getJSON('/game?id=' + id);
  } catch (e) {
    S.gameCache[id] = { id, error: true, achievements: [] };
  }
  gameLoading.delete(id);
  S.gameTick.value++;
}

async function loadBoards(id) {
  if (!id || S.boardsCache[id]) return;
  S.boardsCache[id] = { loading: true };
  try {
    S.boardsCache[id] = { boards: (await getJSON('/boards?id=' + id)).boards || [] };
  } catch (e) {
    S.boardsCache[id] = { error: true };
  }
  S.boardsTick.value++;
}

/* Fetches /game for the running game into S.liveMeta: medians, types,
   rarity, RetroPoints, players and rank. */
let liveMetaLoading = false;
function loadLiveMeta(id) {
  if (liveMetaLoading || !id) return;
  liveMetaLoading = true;
  getJSON('/game?id=' + id).then(g => {
    const byId = {};
    for (const a of g.achievements || [])
      byId[a.id] = { median: a.median || 0, type: a.type || '', players: a.players || 0, rp: a.rp || 0, order: a.order || 0 };
    S.gameCache[id] = g;
    S.gameTick.value++;
    S.liveMeta.value = { id, byId, icon: g.icon || '', game: g };
    liveMetaLoading = false;
  }).catch(() => { liveMetaLoading = false; });
}


/* Fetches /comments for one achievement, once. */
async function loadComments(id) {
  if (!id || S.commentsCache[id]) return;
  S.commentsCache[id] = { loading: true };
  try {
    S.commentsCache[id] = await getJSON('/comments?id=' + id);
  } catch (e) {
    S.commentsCache[id] = { error: true, comments: [] };
  }
  S.commentsTick.value++;
}

function toggleAch(id) {
  const open = S.openAch.value === id ? 0 : id;
  S.openAch.value = open;
  if (open) loadComments(open);
}

function openGame(id) {
  S.gameId.value = id;
  S.tab.value = 'game';
}
