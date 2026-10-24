/* ---- GAME: one set in full, from the Web API --------------------------- */


/* The base game and its subsets as chips, the open one lit. A click
   opens that game id. */
function Family({ g }) {
  if (!(g.subsets || []).length) return null;
  const base = g.base || {};
  return html`<div class="subsets">
    ${base.id > 0 && html`<${Chip} on=${base.id === g.id} onClick=${() => openGame(base.id)}>${t('MAIN SET')}<//>`}
    ${g.subsets.map(sub => html`<${Chip} key=${sub.id} on=${sub.id === g.id} n=${sub.achievements}
      onClick=${() => openGame(sub.id)}>${sub.title.toUpperCase()}<//>`)}
  </div>`;
}

/* "loading..." for the first 12 s, then a note that the client is busy
   and a link that clears the cache entry and fetches again. */
function Loading({ id }) {
  const [slow, setSlow] = useState(false);
  useEffect(() => {
    setSlow(false);
    const tm = setTimeout(() => setSlow(true), 12000);
    return () => clearTimeout(tm);
  }, [id]);
  if (!slow) return html`<${Empty}>${t('loading...')}<//>`;
  return html`<${Empty}>${t('still loading; the client is busy with RetroAchievements')}${' '}
    <a href="#" onClick=${e => { e.preventDefault(); delete S.gameCache[id]; loadGame(id); }}>${t('try again')}</a><//>`;
}

function GameTab() {
  const id = S.gameId.value;
  S.gameTick.value; /* re-render when a fetch lands */
  useEffect(() => { loadGame(id); loadLibrary(); }, [id]);
  /* The award and the hardcore count come from the library entry:
     /game has neither. */
  const lib = (Array.isArray(S.library.value) ? S.library.value : []).find(x => x.id === id);

  if (!id) return html`<div class="tab"><${Empty}>${t('pick a game in the library')}<//></div>`;
  const g = S.gameCache[id];
  if (!g) return html`<div class="tab"><${Loading} id=${id} /></div>`;
  if (g.error) return html`<div class="tab"><${Empty}>${t('could not reach the client')}<//></div>`;

  const sort = S.gameSort.value, filter = S.gameFilter.value, view = S.achView.value;
  const all = g.achievements || [];
  const list = sortAchievements(filterAchievements(all, filter, a => !!a.earned), sort);
  const est = setEstimates(all, a => a.median || 0);
  const players = g.distinct_players || 0;
  const rar = a => players && a.players ? a.players * 100 / players : 0;
  const rank = g.rank && g.rank.rank > 0 ? g.rank.rank : 0;

  return html`<div class="tab">
    <div class="head">
      ${g.icon ? html`<img class="cover" src=${MEDIA + g.icon} alt="" />` : html`<div class="cover" />`}
      <div class="body">
        <h1>${g.title}${lib && awardBadge(lib) && html` <span class="h1-award">${awardBadge(lib)}</span>`}</h1>
        <div class="note" style=${{ marginBottom: '10px' }}>
          ${g.console} · ${t('{n} achievements', { n: all.length })}
          ${players > 0 && html` · ${t('{n} players', { n: players.toLocaleString() })}`}
          ${rank > 0 && html` · <span class="gold">${t('rank #{n}', { n: rank.toLocaleString() })}</span>`}
          ${est.full > 0 && html` · <span class="acc">${est.beaten > 0 && t('beaten ~{time}', { time: medianText(est.beaten) }) + ' · '}${t('full set ~{time}', { time: medianText(est.full) })}</span> <span class="mute">${t('by median unlocks')}</span>`}
        </div>
        <${Bars} labels awarded=${g.awarded} hardcore=${lib ? lib.hardcore : 0} total=${g.total} />
        <div class="sub">
          <a href=${'https://retroachievements.org/game/' + g.id} target="_blank" rel="noopener">${t('on retroachievements.org')}</a>
        </div>
      </div>
    </div>
    <${Family} g=${g} />
    <${ToolsBar} filter=${filter} sort=${sort} view=${view}
      onFilter=${k => { S.gameFilter.value = k; }} onSort=${k => { S.gameSort.value = k; }} onView=${k => setPref('achView', k)} />
    ${view === 'grid'
      ? html`<div class="tiles">${list.map(a => html`<${GameTile} key=${a.id} a=${a} />`)}</div>`
      : list.map(a => html`<${AchRow} key=${a.id} a=${a} state=${a.earned ? 'done' : 'locked'} mode=${a.earned ? a.mode : 0}
      open=${S.openAch.value === a.id} onClick=${() => toggleAch(a.id)}
      right=${html`<${Fragment}>
        ${a.earned ? html`<span class="lead ok">${a.earned.slice(0, 10)}</span>`
                   : html`<span class="lead">${t('locked')}</span>`}
        <${Pts} a=${a} extra=${[a.earned ? html`<${ModeTag} a=${a} />` : null,
                                rar(a) > 0 ? rarityText(rar(a)) : null,
                                a.rp > 0 ? t('RP {n}', { n: a.rp }) : null,
                                a.median > 0 ? '~' + medianText(a.median) : null].filter(Boolean)} />
      <//>`} />`)}
  </div>`;
}
