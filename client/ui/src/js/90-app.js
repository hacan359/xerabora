/* ---- The page: header, status strip, the open tab, footer -------------- */

const linkStats = computed(() => {
  if (mode.value !== 'console') return '';
  const fr = fast.frames.value;
  if (fr) return t('{frames} snapshots, {skipped} skipped', { frames: fr.toLocaleString(), skipped: fast.gaps.value });
  const pk = fast.packets.value;
  return pk ? t('{packets} packets', { packets: pk.toLocaleString() }) : '';
});

/* The footer clock: the console session, or the following session. */
const clockText = computed(() => {
  const m = mode.value;
  if (m === 'console') return fast.seconds.value ? hms(fast.seconds.value) : '';
  if (m === 'follow') return fast.fSince.value ? hms(fast.fSince.value) : '';
  return '';
});

function Header() {
  const tab = S.tab.value;
  const p = S.profile.value;
  /* The separator is inside the points span; a narrow header hides the span. */
  const who = p === null ? ''
    : p.user ? html`<b>${p.user}</b><span class="pts">${t('{user} \u00b7 {points} points', { user: '', points: p.points.toLocaleString() })}</span>`
    : t('no Web API key, see SETTINGS');

  return html`<header class="top"><div class="wrap">
    <a class="brand" href="#" onClick=${e => { e.preventDefault(); S.tab.value = 'live'; }}>
      <span>xe<span class="r">R</span><span class="a">A</span>bora</span>
      ${S.version.value && html`<span class="ver">v${S.version.value}</span>`}
    </a>
    <nav class="tabs">
      ${TABS.map(name => html`<button key=${name} class=${tab === name ? 'on' : ''}
        onClick=${() => { S.tab.value = name; }}>${t(name.toUpperCase())}</button>`)}
    </nav>
    <div class="hdr-right">
      <span class="who">${who}</span>
      <select class="lang" aria-label="Language" value=${lang} onChange=${e => setLang(e.target.value)}>
        ${LANGS.map(([code, name]) => html`<option key=${code} value=${code} title=${name}>${(code || 'en').split('-')[0].toUpperCase()}</option>`)}
      </select>
      ${!REMOTE && html`<${Btn} danger sm onClick=${quit}>${t('QUIT')}<//>`}
    </div>
  </div></header>`;
}

function Status() {
  const s = S.state.value;
  const c = (s && s.console) || {};
  const f = (s && s.follow) || {};
  const li = (s && s.login) || { ok: true };
  const m = mode.value;
  const text = !s ? t('waiting for the console')
    : c.connected ? t('console live at {ip}', { ip: c.ip })
    : f.active ? t('following {game} via RetroAchievements', { game: s.game.title || '' }) + (f.online ? ' · ' + t('playing now') : '')
    : !li.ok ? html`<span class="warn">${t('not signed in to RetroAchievements &mdash; open SETTINGS to sign in')}</span>`
    : t('waiting for the console');

  return html`<div class="status"><div class="wrap">
    <span class=${'dot' + ((c.connected || f.active) ? ' on' : '')} />
    ${m !== 'idle' && html`<span class="mode">${t(m === 'console' ? 'CONSOLE' : 'FOLLOWING')}</span>`}
    <span class="text">${text}</span>
    <span class="counters">${linkStats}</span>
  </div></div>`;
}

async function quit() {
  if (!confirm(t('Quit xerabora? Console telemetry and unlocks stop.'))) return;
  try { await postForm('/quit', {}); } catch (err) { /* it is going away */ }
  S.closed.value = true;
}

function Footer() {
  const s = S.state.value;
  const connected = !!(s && s.console.connected);
  return html`<footer><div class="wrap">
    <${Chip} on cls="static" title="every unlock is softcore">${t('SOFTCORE')}<//>
    <${Chip} on=${connected} cls="static" title="the gold flash on the TV: lit while a console streams">${t('FLASH')}<//>
    <span class="clock">${clockText}</span>
  </div></footer>`;
}

function App() {
  if (S.closed.value)
    return html`<div class="closed">${t('xerabora is closed. This tab can be closed too.')}</div>`;

  const tab = S.tab.value;
  const err = S.error.value;
  return html`<div class="app">
    <${Header} />
    <${Status} />
    <main class="wrap">
      ${err && html`<div class="err">${'page error: ' + err}</div>`}
      ${tab === 'live' ? html`<${LiveTab} />`
        : tab === 'library' ? html`<${LibraryTab} />`
        : tab === 'game' ? html`<${GameTab} />`
        : tab === 'boards' ? html`<${BoardsTab} />`
        : html`<${SettingsTab} />`}
    </main>
    <${Footer} />
    <${Toast} />
    <${PowerOn} />
  </div>`;
}

/* Uncaught errors go to the error box at the top of main. */
function showError(err) { S.error.value = String(err && err.stack ? err.stack : err); }
window.addEventListener('error', e => showError(e.error || e.message));
window.addEventListener('unhandledrejection', e => showError(e.reason));

/* data-mode on <html>: the accent and the washes follow it in CSS. */
effect(() => { document.documentElement.dataset.mode = mode.value; });

/* #tab=boards opens straight on a tab; #game=5761 opens that game's card. */
{
  const q = new URLSearchParams(location.hash.slice(1) || location.search);
  const want = q.get('tab');
  const game = +q.get('game');

  if (game > 0) S.gameId.value = game;
  S.tab.value = TABS.includes(want) ? want : game > 0 ? 'game' : 'live';
}

render(html`<${App} />`, document.getElementById('app'));
loadProfile();
stream();
tick();
