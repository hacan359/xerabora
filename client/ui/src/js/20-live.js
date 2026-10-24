/* ---- LIVE: the console's session, or the account's play elsewhere ------ */

const STATUS = {
  'active':         ['achievement set active, unlocks are tracked', 'ok'],
  'no-hash':        ["image not identified &mdash; run 'RA: check game support' in the game's OPL menu", 'warn'],
  'telemetry-only': ['telemetry only &mdash; no achievement set could be loaded for this image', 'warn'],
  'identifying':    ['loading the achievement set for this image &mdash; a few seconds', 'warn'],
  'stale':          ["console watch list is out of date &mdash; run 'RA: check game support' again", 'warn']
};

/* Frame counter with the rate, or the packet count before the first frame. */
const framesText = computed(() => {
  const fr = fast.frames.value, r = fast.rate.value;
  return fr ? fr.toLocaleString() + (r ? ' · ' + r + '/s' : '')
            : t('{packets} packets', { packets: fast.packets.value.toLocaleString() });
});
const sessionText = computed(() => fast.seconds.value ? hms(fast.seconds.value) : '—');

/* FOLLOW MY PLAY: hidden on REMOTE and without a Web API key. Posts
   /follow and fetches the state again. */
function FollowSwitch({ s }) {
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState('');
  const f = s.follow || {}, li = s.login || {};

  if (REMOTE || !li.webapi) return null;
  const text = err ? err
    : s.console.connected ? t('the console has LIVE while it streams; following takes over when it stops')
    : f.on ? t('LIVE shows the game you are in on any emulator with RetroAchievements, and the unlocks as they land')
           : t('off. Turn it on and LIVE follows your account on any emulator with RetroAchievements');

  const toggle = async () => {
    setBusy(true); setErr('');
    try {
      const r = await postForm('/follow', { on: f.on ? '0' : '1' });
      if (r.ok) await refreshState(); else setErr(r.error || t('failed'));
    } catch (e) { setErr(t('could not reach the client')); }
    setBusy(false);
  };
  return html`<${Switch} on=${!!f.on} label=${t('FOLLOW MY PLAY')} text=${text} busy=${busy} onToggle=${toggle} />`;
}

/* One line per counter in a tile: the string is split on its dots. */
function lines(text) {
  return text.split(' \u00b7 ').map((p, i) => html`${i ? html`<br />` : ''}${p}`);
}

/* The three loss counters; the component redraws when any one moves. */
function Lost() {
  return lines(t('{gaps} skipped \u00b7 {dupes} repeated \u00b7 {torn} torn',
                 { gaps: fast.gaps.value, dupes: fast.dupes.value, torn: fast.torn.value }));
}

/* Sends the console back to the loader menu: the in-game reset, from
   the PC instead of the six-button combo. Local only, like QUIT. */
async function resetConsole() {
  if (!confirm(t('Close the game and return the console to the loader menu?'))) return;
  try {
    const r = await postForm('/reset', {});
    if (r && r.ok === false) alert(t('The console has not been seen yet.'));
  } catch (err) { alert(t('The client did not answer.')); }
}

/* The pipeline status and the rich presence bar. */
function LiveBanners({ s }) {
  const c = s.console;
  const st = c.connected || c.packets ? STATUS[c.status] : null;
  const f = s.follow || {};
  const rp = s.game.rp || (f.active ? f.rp : '');
  return html`<${Fragment}>
    ${st && html`<div class=${'banner ' + st[1]}>${t(st[0])}</div>`}
    ${rp && html`<div class="rpbar"><span class="dot" />${rp}</div>`}
  <//>`;
}

/* The console's own numbers from the telemetry header. */
function Hardware() {
  const h = fast.hw.value || {};
  const has = h.link || h.send_us || h.ee_share || h.rxq_max;
  if (!has) return null;
  const link = h.link ? (h.link.slice(0, 3) === '999' ? t('no link') : parseInt(h.link, 10) + ' Mbit ' + (h.link[3] === 'F' ? t('full duplex') : t('half duplex'))) : '—';
  return html`<${Fragment}>
    <${Eyebrow}>${t('HARDWARE')}<//>
    <div class="stats">
      <${Cell} k=${t('LINK MODE')} v=${link} />
      <${Cell} k=${t('EE READ')} v=${h.ee_share > 0 ? h.ee_share.toFixed(1) + ' %' : '—'} />
      <${Cell} k=${t('EE SKIPPED')} v=${h.ee_skipped || 0} />
      <${Cell} k=${t('TORN ON CONSOLE')} v=${h.console_torn || 0} />
      <${Cell} k=${t('SEND TIME')} v=${lines(t('{us} us &middot; max {mx} us', { us: h.send_us || 0, mx: h.send_max_us || 0 }))} />
      <${Cell} k=${t('SEND ERRORS')} v=${lines(t('{fail} failed &middot; {sk} deferred', { fail: h.send_fail || 0, sk: h.send_deferred || 0 }))} cls=${h.send_fail ? 'warn' : ''} />
      <${Cell} k=${t('RX FIFO')} v=${lines(t('{q} now &middot; max {mx}', { q: h.rxq || 0, mx: h.rxq_max || 0 }))} />
      <${Cell} k=${t('LAST ERROR')} v=${h.send_err ? h.send_err : '—'} />
    </div>
  <//>`;
}

/* CONSOLE panel: link, serial, counters, watch list, session clock. */
function ConsolePanel({ s }) {
  const c = s.console;
  if (!c.connected && !c.packets) return null;
  if (!c.connected && (s.follow || {}).active) return null;

  const w = c.watch || {};
  const watch = w.addresses
    ? lines(t('{addresses} addr &middot; {bytes} B &middot; {parts} parts',
              { addresses: w.addresses, bytes: w.bytes, parts: w.parts }))
    : '—';

  return html`<${Fragment}>
    <${Eyebrow}>${t('CONSOLE')}<//>
    <div class="stats">
      <${Cell} k=${t('LINK')} v=${c.connected ? c.ip : html`<span class="mute">${t('lost')}</span>`} />
      <${Cell} k=${t('GAME')} v=${s.game.serial || '—'} />
      <${Cell} k=${t('SNAPSHOTS')} v=${framesText} />
      <${Cell} k=${t('LOST')} v=${html`<${Lost} />`} />
      <${Cell} k=${t('WATCH LIST')} v=${watch} />
      <${Cell} k=${t('SESSION')} v=${sessionText} />
    </div>
    ${s.game.hash && html`<div class="hash">${t('hash {hash}', { hash: s.game.hash })}</div>`}
    <${Hardware} />
    ${!REMOTE && c.connected && html`<div class="subsets"><${Btn} danger sm onClick=${resetConsole}>${t('RESET CONSOLE')}<//></div>`}
  <//>`;
}

/* FOLLOWING panel: the rich presence line, the poll, this session's unlocks. */
function FollowPanel({ s }) {
  const f = s.follow;
  if (!f || !f.active) return null;
  const seen = f.online ? html`<span class="ok">${t('playing now')}</span>`
             : f.rp_date ? f.rp_date
             : fast.fSeen.value >= 0 ? agoText(fast.fSeen.value) : '—';
  const session = f.session || [];

  return html`<${Fragment}>
    <${Eyebrow}>${t('FOLLOWING')}<//>
    ${!f.rp && html`<div class="banner">${t('no rich presence line yet')}</div>`}
    <div class="stats">
      <${Cell} k=${t('PLATFORM')} v=${s.game.console || '—'} />
      <${Cell} k=${t('LAST SEEN')} v=${seen} />
      <${Cell} k=${t('SOURCE')} v=${t('RetroAchievements &middot; every 20 s')} />
      <${Cell} k=${t('UNLOCKS')} v=${session.length} />
    </div>
    ${f.failures ? html`<div class="note warn">${t('the Web API is not answering; retrying')}</div>` : null}
    ${session.length > 0 && html`<${Eyebrow}>${t('THIS SESSION')}<//>
      ${session.map(u => html`<div class="row done" key=${u.id}>
        <img class="badge" src=${BADGE + u.badge + '.png'} alt="" />
        <div class="body"><div class="t">${u.title}</div>
          <div class="d">${agoText(u.ago)}${u.hardcore ? ' · ' + t('hardcore') : ''}</div></div>
        <div class="m"><span class="lead ok">+${u.points}</span></div>
      </div>`)}`}
  <//>`;
}

/* Names the source under the title: the console with its address,
   or the emulator. */
function SourceTag({ s }) {
  const c = s.console || {}, f = s.follow || {};
  if (c.connected) return html`<span class="src on">${t('CONSOLE')} <span class="num">${c.ip}</span></span>`;
  if (f.active) return html`<span class="src web">${t('EMULATOR')} <span class="mute">${t('via RetroAchievements')}</span></span>`;
  if (c.packets) return html`<span class="src">${t('CONSOLE')} <span class="mute">${t('lost')}</span></span>`;
  return null;
}

/* The game's head: cover, title, progress bar, counts. */
function LiveHead({ s, list, done, ptsDone, ptsAll, icon }) {
  return html`<div class="head">
    <${Cover} src=${icon ? MEDIA + icon : ''} />
    <div class="body">
      <h1>${s.game.title || s.game.serial}</h1>
      <${Bar} value=${pct(done, list.length)} />
      <div class="sub">
        <${SourceTag} s=${s} />
        <span>${s.game.checked_only ? t('from an image check, not from a running game') : (s.game.serial || s.game.console || '')}</span>
        <span class="right"><b>${done}</b> / ${list.length} <span class="mute">·</span> <span class="gold">${ptsDone}</span><span class="mute"> / ${t('{points} pts', { points: ptsAll })}</span></span>
      </div>
    </div>
  </div>`;
}

/* ALL ACHIEVEMENTS: one list, or one per subset when there are several. */
function AchievementGroups({ list, subsets, row }) {
  if (!subsets || subsets.length < 2) return list.map(row);
  return subsets.map(sub => {
    const mine = list.filter(a => a.subset === sub.id);
    if (!mine.length) return null;
    const done = mine.filter(a => a.state === 2).length;
    return html`<${Fragment} key=${sub.id}>
      <${Eyebrow} count=${done + ' / ' + mine.length}>${sub.title.toUpperCase()}<//>
      ${mine.map(row)}
    <//>`;
  });
}

/* One row of ALL ACHIEVEMENTS; reads the achievement's live signal for
   the measured value and the progress. */
function LiveRow({ a, missed, hidden, med, lm }) {
  const isDone = a.state === 2;
  /* state 3 is rc_client's disabled state: 'not on hardware'. */
  const na = a.state === 3;
  const lv = liveVal(a.id).value;
  const near = !isDone && !na && lv.p > 0;
  const state = isDone ? 'done' : na ? 'na' : near ? 'near' : 'locked';
  return html`<${AchRow} a=${a} state=${state} mode=${isDone ? a.mode : 0}
    open=${S.openAch.value === a.id} onClick=${() => toggleAch(a.id)}
    below=${!isDone && !na && html`<${Mini} id=${a.id} />`}
    right=${html`<${Fragment}>
      ${isDone ? html`<span class="lead ok">${t('unlocked')}</span>`
        : na ? html`<span class="lead warn">${t('not on hardware')}</span>`
        : lv.m ? html`<span class="lead acc">${lv.m}</span>`
        : html`<span class="lead">${t('locked')}</span>`}
      <${Pts} a=${a} extra=${[isDone ? html`<${ModeTag} a=${a} />` : null,
                               ...rarityExtras(lm, a),
                               med > 0 ? '~' + medianText(med) : null,
                               hidden ? html`<span class="mute">${t('hidden')}</span>`
                               : missed ? html`<span class="warn">${t('possibly skipped')}</span>` : null].filter(Boolean)} />
    <//>`} />`;
}

/* Where an unlock was earned: 1 softcore, 2 hardcore, 3 both (a hardcore unlock sets both). */
function modeText(a) {
  return a.mode === 3 ? t('hardcore and softcore') : a.mode === 2 ? t('hardcore') : t('softcore');
}
function ModeTag({ a }) { return html`<span class=${a.mode >= 2 ? 'gold' : 'acc'}>${modeText(a)}</span>`; }

/* The set as the site shows it: badges only, the words in the tooltip. */
function AchTile({ a, state, tip }) {
  return html`<img class=${'tile ' + state + (state === 'done' && a.mode ? ' m' + a.mode : '')}
    src=${badgeUrl(a, state !== 'done')} alt=${a.title} title=${tip} loading="lazy" />`;
}

function LiveTile({ a, med, missed }) {
  const isDone = a.state === 2, na = a.state === 3;
  const lv = liveVal(a.id).value;
  const near = !isDone && !na && lv.p > 0;
  const state = isDone ? 'done' : na ? 'na' : near ? 'near' : 'locked';
  const tip = [a.title, t('{points} pts', { points: a.points }), a.description, lv.m || '',
               isDone ? modeText(a) : '', med > 0 ? '~' + medianText(med) : '',
               na ? t('not on hardware') : '', missed ? t('possibly skipped') : ''].filter(Boolean).join('\n');
  return html`<${AchTile} a=${a} state=${state} tip=${tip} />`;
}

function GameTile({ a }) {
  const tip = [a.title, t('{points} pts', { points: a.points }), a.description,
               a.earned ? a.earned.slice(0, 10) + ' · ' + modeText(a) : '',
               a.median > 0 ? '~' + medianText(a.median) : ''].filter(Boolean).join('\n');
  return html`<${AchTile} a=${a} state=${a.earned ? 'done' : 'locked'} tip=${tip} />`;
}


function AchievementTiles({ list, subsets, medOf, missed }) {
  const tile = a => html`<${LiveTile} key=${a.id} a=${a} med=${medOf(a)} missed=${missed.has(a.id)} />`;
  if (!subsets || subsets.length < 2) return html`<div class="tiles">${list.map(tile)}</div>`;
  return subsets.map(sub => {
    const mine = list.filter(a => a.subset === sub.id);
    if (!mine.length) return null;
    const done = mine.filter(a => a.state === 2).length;
    return html`<${Fragment} key=${sub.id}>
      <${Eyebrow} count=${done + ' / ' + mine.length}>${sub.title.toUpperCase()}<//>
      <div class="tiles">${mine.map(tile)}</div>
    <//>`;
  });
}


/* Everything the achievement views need, computed once per render. */
function liveModel(s) {
  const list = s.game.achievements || [];
  const done = list.filter(a => a.state === 2).length;
  const ptsDone = list.filter(a => a.state === 2).reduce((n, a) => n + (a.points || 0), 0);
  const ptsAll = list.reduce((n, a) => n + (a.points || 0), 0);
  const lm = S.liveMeta.value;
  const meta = lm.id === s.game.id ? lm.byId : null;
  const lmeta = meta ? lm : null;
  const medOf = a => meta ? ((meta[a.id] || {}).median || 0) : 0;
  const missed = meta ? possiblyMissed(list, medOf) : new Set();
  const position = meta ? Math.max(0, ...list.filter(a => a.state === 2).map(medOf)) : 0;
  const est = meta ? setEstimates(list, medOf) : null;
  const rpDone = list.filter(a => a.state === 2).reduce((n, a) => n + rpOf(lmeta, a), 0);
  const rank = lmeta && lm.game && lm.game.rank && lm.game.rank.rank > 0 ? lm.game.rank.rank : 0;
  const hidden = new Set(S.hidden.value[s.game.id] || []);
  const icon = (meta && lm.icon) || s.game.icon || '';
  return { list, done, ptsDone, ptsAll, lm, meta, lmeta, medOf, missed, position, est, rpDone, rank, hidden, icon };
}

/* The achievement block: filter bar, UP NEXT, then the list or the
   grid. LIVE and STREAM both draw it. */
function AchBlock({ s, m }) {
  const { list, done, meta, lm, lmeta, medOf, missed, position, hidden } = m;

  /* UP NEXT: the nearest locked achievements by median, drawn big.
     Hidden ones are left out and the next median takes the slot; the
     chip in the label brings them all back. */
  let ahead = null;
  if (meta) {
    const picked = upNextPick(list.filter(a => !hidden.has(a.id)), medOf);
    const restore = hidden.size > 0
      ? html`<span class="chip sm" onClick=${() => showHidden(s.game.id)}>${t('{n} hidden', { n: hidden.size })} · ${t('SHOW ALL')}</span>`
      : null;
    const shut = S.upNext.value === 'shut';
    if (picked.next.length || restore) {
      ahead = html`<h2 class=${'eyebrow fold' + (shut ? ' shut' : '')} onClick=${() => setPref('upNext', shut ? 'open' : 'shut')}
          title=${shut ? t('show UP NEXT') : t('hide UP NEXT')}>
          <${Icon} name="chevron" />${t('UP NEXT &middot; BY MEDIAN UNLOCK TIME')}
          <span class="count" onClick=${e => e.stopPropagation()}>${shut ? picked.next.length : restore}</span></h2>
        ${!shut && picked.next.map(a => html`<${AchRow} key=${a.id} a=${a} state="near" big
          open=${S.openAch.value === a.id} onClick=${() => toggleAch(a.id)}
          below=${html`<${Mini} id=${a.id} />`}
          right=${html`<${Fragment}>
            <${Measured} id=${a.id} />
            ${a === picked.prog && html`<span class="tag acc">${t('next in progression')}</span>`}
            ${missed.has(a.id) ? html`<span class="tag warn">${t('possibly skipped')}</span>`
              : medOf(a) <= position ? html`<span class="tag warn">${t('usually done by now')}</span>` : null}
            <${Pts} a=${a} extra=${[...rarityExtras(lmeta, a), '~' + medianText(medOf(a))]} />
            <span class="hide" onClick=${e => { e.stopPropagation(); hideAch(s.game.id, a.id); }} title=${t('hide from UP NEXT')}>✕ ${t('HIDE')}</span>
          <//>`} />`)}`;
    }
  }

  /* A filter or an order other than the author's shows one list
     without subset headings. */
  const filter = S.liveFilter.value, sort = S.liveSort.value, view = S.achView.value;
  const plain = filter === 'all' && sort === 'author';
  const getMeta = a => (meta && meta[a.id]) || {};
  const shown = plain ? list
    : sortAchievements(filterAchievements(list.slice(), filter, a => a.state === 2), sort, getMeta);
  const showAll = !!ahead || (plain && (s.game.subsets || []).length > 1);
  const row = a => html`<${LiveRow} key=${a.id} a=${a} med=${medOf(a)} missed=${missed.has(a.id)} hidden=${hidden.has(a.id)} lm=${lmeta} />`;

  return html`<${Fragment}>
    <${ToolsBar} filter=${filter} sort=${sort} view=${view}
      onFilter=${k => { S.liveFilter.value = k; }} onSort=${k => setPref('liveSort', k)} onView=${k => setPref('achView', k)} />
    ${ahead}
    ${showAll && html`<${Eyebrow} count=${done + ' / ' + list.length}>${t('ALL ACHIEVEMENTS')}<//>`}
    ${!shown.length ? html`<${Empty}>${t('nothing matches')}<//>`
      : view === 'grid'
      ? html`<${AchievementTiles} list=${shown} subsets=${plain ? s.game.subsets : null} medOf=${medOf} missed=${missed} />`
      : html`<${AchievementGroups} list=${shown} subsets=${plain ? s.game.subsets : null} row=${row} />`}
  <//>`;
}

function LiveTab() {
  const s = S.state.value;
  if (!s) return html`<${Empty}>${t('loading...')}<//>`;

  const list = s.game.achievements || [];
  const f = s.follow || {};
  const c = s.console || {};

  if (!list.length) {
    return html`<div class="tab">
      <${LiveBanners} s=${s} />
      <${FollowSwitch} s=${s} /><${ConsolePanel} s=${s} />
      <${Empty}>${s.console.connected
        ? t('console connected, waiting for a game with achievements')
        : f.on && (s.login || {}).webapi
          ? t('following your RetroAchievements activity: start a game on the console, or in any emulator with RetroAchievements on, and it shows up here within half a minute')
          : t('start a game on the console, or browse your library above')}<//>
    </div>`;
  }

  const m = liveModel(s);
  const { done, ptsDone, ptsAll, est, position, rpDone, rank } = m;

  const strip = html`<div class="stats">
    <${Cell} k=${t('ACHIEVEMENTS')} v=${done + ' / ' + list.length} />
    <${Cell} k=${t('POINTS')} v=${html`<span class="gold">${ptsDone}</span> / ${ptsAll}`} />
    <${Cell} k=${t('RETROPOINTS')} v=${rpDone > 0 ? rpDone.toLocaleString() : '—'} />
    <${Cell} k=${t('RANK IN GAME')} v=${rank ? '#' + rank.toLocaleString() : '—'} />
    <${Cell} k=${t('BEATEN AT')} v=${est && est.beaten ? '~' + medianText(est.beaten) : '—'} />
    <${Cell} k=${t('FULL SET AT')} v=${est && est.full ? '~' + medianText(est.full) : '—'} />
    <${Cell} k=${t('YOUR POSITION')} v=${position ? '~' + medianText(position) : '—'} />
    <${Cell} k=${t('MISSABLES LEFT')} v=${list.filter(a => a.state !== 2 && achType(a) === 'missable').length} />
  </div>`;

  /* Sub-tabs: achievements, leaderboards, and the console while a
     console is the source. */
  const sub = S.liveSub.value;
  const hasConsole = !!(c.connected || (c.packets && !f.active));
  const nTrack = (s.game.tracking || []).length;
  const nav = html`<div class="subnav">
    <${Chip} on=${sub === 'ach'} onClick=${() => setPref('liveSub', 'ach')} n=${done + ' / ' + list.length}>${t('ACHIEVEMENTS')}<//>
    <${Chip} on=${sub === 'lb'} onClick=${() => setPref('liveSub', 'lb')} n=${nTrack || null}>${t('LEADERBOARDS')}<//>
    ${hasConsole && html`<${Chip} on=${sub === 'src'} onClick=${() => setPref('liveSub', 'src')} n=${c.connected ? c.ip : null}>${t('CONSOLE')}<//>`}
  </div>`;

  return html`<div class="tab">
    <${LiveBanners} s=${s} />
    <${FollowSwitch} s=${s} />
    <${LiveHead} s=${s} list=${list} done=${done} ptsDone=${ptsDone} ptsAll=${ptsAll} icon=${m.icon} />
    ${strip}
    ${nav}
    ${sub === 'lb' ? html`<${BoardsBody} id=${s.game.id} />`
      : sub === 'src' && hasConsole ? html`<${ConsolePanel} s=${s} />`
      : html`<${Fragment}><${FollowPanel} s=${s} /><${AchBlock} s=${s} m=${m} /><//>`}
  </div>`;
}
