/* ---- STREAM: the page as one screen for a stream ----------------------- */

/* Rarity as a share of the game's players, and RetroPoints; 0 until
   /game answers. */
function rarityOf(lm, a) {
  const g = lm && lm.game, m = lm && lm.byId[a.id];
  return g && m && g.distinct_players ? m.players * 100 / g.distinct_players : 0;
}
function rpOf(lm, a) { const m = lm && lm.byId[a.id]; return m ? m.rp || 0 : 0; }
function rarityText(r) { return r <= 0 ? '' : t('{pct}% of players', { pct: r < 1 ? '<1' : Math.round(r) }); }

/* The two lines LIVE and STREAM add to an achievement's right column. */
function rarityExtras(lm, a) {
  const r = rarityOf(lm, a), rp = rpOf(lm, a);
  return [r > 0 ? rarityText(r) : null, rp > 0 ? t('RP {n}', { n: rp }) : null].filter(Boolean);
}

function noGameText(s) {
  return s.console.connected ? t('console connected, waiting for a game with achievements')
    : (s.follow || {}).active ? t('following via RetroAchievements')
    : t('waiting for the console');
}

/* The red dot: lit while a source feeds the page. */
function LiveChip({ s }) {
  const on = !!(s && (s.console.connected || (s.follow || {}).active));
  return html`<span class=${'chip static live' + (on ? ' on' : '')}><span class="dot" />${on ? t('LIVE') : t('OFFLINE')}</span>`;
}

/* The stream view: the game's head and the achievement block. */
function StreamView() {
  const s = S.state.value;
  const bar = html`<div class="sbar">
    <a class="brand" href="#" onClick=${e => { e.preventDefault(); go('tabs'); }}><span>xe<span class="r">R</span><span class="a">A</span>bora</span></a>
    <span class="mode">${t('STREAM')}</span>
    <${LiveChip} s=${s} /><span class="sp"></span>
    <${Btn} sm onClick=${() => go('tabs')}>${t('BACK TO TABS')}<//></div>`;
  if (!s) return html`<div class="stream">${bar}<${Empty}>${t('loading...')}<//></div>`;

  const list = s.game.achievements || [];
  if (!list.length) return html`<div class="stream">${bar}<${LiveBanners} s=${s} /><${Empty}>${noGameText(s)}<//></div>`;

  const m = liveModel(s);
  return html`<div class="stream">
    ${bar}
    <${LiveBanners} s=${s} />
    <${LiveHead} s=${s} list=${list} done=${m.done} ptsDone=${m.ptsDone} ptsAll=${m.ptsAll} icon=${m.icon} />
    <${AchBlock} s=${s} m=${m} />
  </div>`;
}
