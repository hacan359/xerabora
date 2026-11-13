/* ---- SETTINGS: account, Web API key, the network switch ---------------- */

/* The tab re-renders on this key: the login and the network setting,
   not the whole state. */
const settingsKey = computed(() => {
  const s = S.state.value;
  if (!s) return '';
  const li = s.login || {}, lan = s.lan || {}, notice = s.notice || {};
  const dc = s.discord || {}, diag = s.diag || {};
  return [li.ok, li.user, li.webapi, lan.on, lan.url, notice.on, dc.available, dc.on, diag.test, diag.dir].join('|');
});

function Msg({ text, good }) {
  return html`<div class=${'msg ' + (good ? 'ok' : 'warn')}>${text}</div>`;
}

function AccountBlock({ li }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const signIn = async e => {
    e.preventDefault();
    const form = e.target;
    setBusy(true); setMsg([t('signing in...'), true]);
    try {
      const r = await postForm('/login', { user: form.user.value.trim(), password: form.password.value });
      if (r.ok) { setMsg(['', true]); await refreshState(); }
      else setMsg([r.error || t('failed'), false]);
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };
  const signOut = async () => {
    try { await postForm('/logout', {}); } catch (err) { /* gone already */ }
    try { await refreshState(); } catch (err) { /* the poll catches up */ }
  };

  return html`<div class="block">
    <${Eyebrow}>${t('RETROACHIEVEMENTS ACCOUNT')}<//>
    ${li.ok
      ? html`<div>${tv('signed in as {user}', { user: html`<span class="ok">${li.user}</span>` })}</div>
          <div class="note">${t('unlocks from the console are credited to this account')}</div>
          <${Btn} onClick=${signOut}>${t('SIGN OUT')}<//>`
      : html`<div class="note">${t('the account your unlocks are credited to; only a login token is stored')}</div>
          <form onSubmit=${signIn}>
            <label>${t('username')}</label><input class="f" name="user" autocomplete="username" />
            <label>${t('password')}</label><input class="f" name="password" type="password" autocomplete="current-password" />
            <${Btn} type="submit" primary disabled=${busy}>${t('SIGN IN')}<//>
          </form>`}
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}

function KeyBlock({ li }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const save = async e => {
    e.preventDefault();
    const form = e.target;
    setBusy(true); setMsg([t('saving...'), true]);
    try {
      const r = await postForm('/apikey', { key: form.key.value.trim() });
      if (r.ok) {
        form.key.value = '';
        S.library.value = null;
        loadProfile();
        await refreshState();
        setMsg([t('saved'), true]);
      } else setMsg([r.error || t('failed'), false]);
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  return html`<div class="block">
    <${Eyebrow}>${t('WEB API KEY')}<//>
    <div class="note">${tv('a read-only key that fills the library, leaderboards and profile. Get it at {link}, section "Keys", and paste it here.',
      { link: html`<a href="https://retroachievements.org/settings" target="_blank" rel="noopener">retroachievements.org/settings</a>` })}</div>
    <form onSubmit=${save}>
      <label>${t('key')} ${li.webapi && html`<span class="ok">${t('(saved, paste to replace)')}</span>`}</label>
      <input class="f" name="key" autocomplete="off" />
      <${Btn} type="submit" primary disabled=${busy}>${t('SAVE KEY')}<//>
    </form>
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}

function LanBlock({ lan }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const flip = async () => {
    setBusy(true); setMsg([t(lan.on ? 'closing...' : 'opening...'), true]);
    try {
      const r = await postForm('/lan', { on: lan.on ? '0' : '1' });
      if (!r.ok) { setMsg([r.error || t('failed'), false]); setBusy(false); return; }
      /* The listener reopens on the next request. */
      await new Promise(res => setTimeout(res, 700));
      await refreshState();
      setMsg(['', true]);
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  return html`<div class="block">
    <${Eyebrow}>${t('OTHER DEVICES')}<//>
    ${lan.on
      ? html`<div>${t('open to the network')}${lan.url
            ? html`: <a class="ok" href=${lan.url}>${lan.url}</a>`
            : ' ' + t('(no network address found yet)')}</div>
          <div class="note">${t('type that address into a phone or tablet on the same Wi-Fi and add the page to the home screen. Other devices watch; settings and QUIT stay here. On Windows, allow xerabora through the firewall when it asks.')}</div>
          <${Btn} onClick=${flip} disabled=${busy}>${t('CLOSE TO THIS PC')}<//>`
      : html`<div class="note">${t('the page is reachable from this PC only. Open it to the network and any phone, tablet or second PC on the same Wi-Fi can watch this tracker.')}</div>
          <${Btn} onClick=${flip} primary disabled=${busy}>${t('OPEN TO THE NETWORK')}<//>`}
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}

function NoticeBlock({ notice }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const flip = async () => {
    setBusy(true); setMsg(['', true]);
    try {
      const r = await postForm('/notice', { on: notice.on ? '0' : '1' });
      if (!r.ok) { setMsg([r.error || t('failed'), false]); setBusy(false); return; }
      await refreshState();
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  return html`<div class="block">
    <${Eyebrow}>${t('UNLOCK ON THE TV')}<//>
    ${notice.on
      ? html`<div>${t('an unlock shows on the TV: the badge and the title over the game.')}</div>
          <div class="note">${t('some games break their picture under it. Turn it off for those; unlocks still show here.')}</div>
          <${Btn} onClick=${flip} disabled=${busy}>${t('KEEP IT OFF THE TV')}<//>`
      : html`<div class="note">${t('unlocks show on this page only; the TV stays untouched.')}</div>
          <${Btn} onClick=${flip} primary disabled=${busy}>${t('SHOW ON THE TV')}<//>`}
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}

function DiscordBlock({ discord }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const flip = async () => {
    setBusy(true); setMsg(['', true]);
    try {
      const r = await postForm('/discord', { on: discord.on ? '0' : '1' });
      if (!r.ok) { setMsg([r.error || t('failed'), false]); setBusy(false); return; }
      await refreshState();
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  return html`<div class="block">
    <${Eyebrow}>DISCORD<//>
    ${discord.on
      ? html`<div>${t('your Discord profile shows the game, what you are doing in it and the last unlock.')}</div>
          <${Btn} onClick=${flip} disabled=${busy}>${t('HIDE FROM DISCORD')}<//>`
      : html`<div class="note">${t('Discord shows nothing about your play.')}</div>
          <${Btn} onClick=${flip} primary disabled=${busy}>${t('SHOW ON DISCORD')}<//>`}
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}

/* Test mode and the report of the last minute, for looking into an
   achievement that fired when it should not have, or did not fire. */
function DiagBlock({ diag }) {
  const [msg, setMsg] = useState(['', true]);
  const [busy, setBusy] = useState(false);

  const flip = async () => {
    setBusy(true); setMsg(['', true]);
    try {
      const r = await postForm('/diag', { on: diag.test ? '0' : '1' });
      if (!r.ok) { setMsg([r.error || t('failed'), false]); setBusy(false); return; }
      /* The client switches on its next turn, within a second. */
      await new Promise(done => setTimeout(done, 1200));
      await refreshState();
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  const save = async () => {
    setBusy(true); setMsg(['', true]);
    try {
      const r = await postForm('/diag/save', {});
      setMsg(r.ok ? [t('saved: {path}', { path: r.path }), true] : [t(r.error || 'failed'), false]);
    } catch (err) { setMsg([t('could not reach the client'), false]); }
    setBusy(false);
  };

  return html`<div class="block">
    <${Eyebrow}>${t('DIAGNOSTICS')}<//>
    ${diag.test
      ? html`<div>${t('test mode is on: every achievement can trigger again, and nothing is sent to RetroAchievements. Each one that triggers is written to a file with its conditions and the memory it read.')}</div>
          <${Btn} onClick=${flip} disabled=${busy}>${t('BACK TO NORMAL PLAY')}<//>`
      : html`<div class="note">${t('test mode checks achievements without touching your account: every achievement can trigger again, nothing is sent to RetroAchievements, and each trigger is written to a file.')}</div>
          <${Btn} onClick=${flip} primary disabled=${busy}>${t('START TEST MODE')}<//>`}
    <div class="note">${t('an achievement did not unlock, or unlocked by itself? Save the last minute right after it happens and attach the file to your report.')}</div>
    <${Btn} onClick=${save} disabled=${busy}>${t('SAVE THE LAST MINUTE')}<//>
    ${diag.dir && html`<div class="note">${t('files go to {dir}', { dir: diag.dir })}</div>`}
    <${Msg} text=${msg[0]} good=${msg[1]} />
  </div>`;
}


/* ABOUT: the version this client runs, the newest release on GitHub,
   and where to go with a problem. The release list includes
   prereleases, which /releases/latest leaves out. */
const REPO = 'https://github.com/hacan359/xerabora';
const latestRelease = signal(null);
let latestAsked = false;

function askLatest() {
  if (latestAsked) return;
  latestAsked = true;
  fetch('https://api.github.com/repos/hacan359/xerabora/releases?per_page=1')
    .then(r => r.json())
    .then(list => { if (Array.isArray(list) && list[0]) latestRelease.value = list[0]; })
    .catch(() => { latestAsked = false; });
}

function AboutBlock() {
  askLatest();
  const v = S.version.value;
  const rel = latestRelease.value;
  const newer = rel && v && rel.tag_name !== 'v' + v;

  return html`<div class="block">
    <${Eyebrow}>${t('ABOUT')}<//>
    <div>xeRAbora <span class="ok">${v ? 'v' + v : ''}</span></div>
    ${rel && html`<div class="note">${newer
      ? tv('{tag} is out: {link}', { tag: rel.tag_name, link: html`<a class="ok" href=${rel.html_url} target="_blank" rel="noopener">${t('download')}</a>` })
      : t('this is the latest release')}</div>`}
    <div class="note">${t('something does not work? Read the guide first, then open an issue with the debug build\'s logs.')}</div>
    <div class="links">
      <a href=${REPO + '/blob/main/docs/TROUBLESHOOTING.md'} target="_blank" rel="noopener">${t('BEFORE YOU REPORT')}</a>
      <a href=${REPO + '/issues'} target="_blank" rel="noopener">${t('ISSUES')}</a>
      <a href="https://discord.gg/JZVNPF7cS2" target="_blank" rel="noopener">DISCORD</a>
      <a href=${REPO + '/releases'} target="_blank" rel="noopener">${t('RELEASES')}</a>
      <a href="https://hacan359.github.io/xerabora/" target="_blank" rel="noopener">${t('PROJECT PAGE')}</a>
    </div>
    <div class="note">${t('Built on Open PS2 Loader, rcheevos and ps2sdk. The Android app by MrRexD. The PC client is MIT; the OPL fork keeps OPL\'s AFL 3.0.')}</div>
  </div>`;
}

/* The look of the page, per browser like the language. */
function ThemeBlock() {
  return html`<div class="block">
    <${Eyebrow}>${t('THEME')}<//>
    <div class="subsets">
      ${THEMES.map(([k, label]) => html`<${Chip} key=${k} on=${S.theme.value === k} onClick=${() => setPref('theme', k)}>${t(label)}<//>`)}
    </div>
    <div class="note">${t('kept in this browser, like the language')}</div>
  </div>`;
}

function SettingsTab() {
  settingsKey.value;
  const s = S.state.peek();
  const li = (s && s.login) || { ok: false, user: '', webapi: false };
  const lan = (s && s.lan) || { on: false, url: '' };
  const notice = (s && s.notice) || { on: true };
  const discord = (s && s.discord) || { available: false, on: false };
  const diag = (s && s.diag) || { test: false, dir: '' };

  if (REMOTE) {
    return html`<div class="tab"><div class="set">
      <${Eyebrow}>${t('OTHER DEVICES')}<//>
      <div>${tv('this page is open from {host}', { host: html`<span class="ok">${location.host}</span>` })}</div>
      <div class="note">${t('account, Web API key, the network switch and QUIT are changed on the PC that runs xerabora; from here you watch.')}</div>
      ${li.ok && html`<div style=${{ marginTop: '14px' }}>${tv('signed in as {user}', { user: html`<span class="ok">${li.user}</span>` })}</div>`}
      <div style=${{ marginTop: '26px' }}><${ThemeBlock} /></div>
      <${AboutBlock} />
    </div></div>`;
  }

  return html`<div class="tab"><div class="set">
    <${AccountBlock} li=${li} />
    <${KeyBlock} li=${li} />
    <${LanBlock} lan=${lan} />
    <${NoticeBlock} notice=${notice} />
    ${discord.available && html`<${DiscordBlock} discord=${discord} />`}
    <${DiagBlock} diag=${diag} />
    <${ThemeBlock} />
    <${AboutBlock} />
  </div></div>`;
}
