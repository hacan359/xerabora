/* ---- OVERLAY: the stream screen as one solid panel for OBS ------------- */

/* Layouts: the stage size and boxes in stage pixels, [x, y, w, h].
   game, cam and chat are cut-outs; ach, head, status and foot sit on
   the panel. A missing box is not drawn. */
const OV_LAYOUTS = {
  a: { size: [1920, 1080], game: [20, 20, 1236, 927], cam: [1276, 20, 624, 351],
       ach: [1276, 383, 624, 330], chat: [1276, 725, 624, 306], head: [36, 959, 1204, 72],
       status: [24, 1046, 1872, 24] },
  b: { size: [1920, 1080], game: [20, 20, 1488, 837], cam: [20, 873, 281, 158],
       ach: [1528, 20, 372, 440], chat: [1528, 472, 372, 559], head: [325, 873, 1175, 158],
       status: [24, 1046, 1872, 24], stack: true },
  c: { size: [1920, 1080], game: [552, 20, 1348, 1011], cam: [20, 20, 512, 288],
       head: [28, 320, 496, 112], ach: [20, 444, 512, 300], chat: [20, 756, 512, 275],
       status: [24, 1046, 1872, 24], stack: true },
  v: { size: [1080, 1920], cam: [20, 20, 1040, 440], game: [20, 476, 1040, 780],
       head: [36, 1272, 1008, 104], ach: [20, 1388, 1040, 156], status: [24, 1562, 1032, 24],
       foot: [20, 1660, 1040, 140], stack: true, narrow: true }
};
const OV_HOLES = [['game', 'GAME'], ['cam', 'CAMERA'], ['chat', 'CHAT']];

/* The choices on the stream screen: key, label, tooltip. 'column' is
   the page column STREAM had before. */
const STREAM_LAYOUTS = [
  ['column', 'COLUMN', 'the game and the achievements as a page column'],
  ['a', '4:3', '4:3 game; camera, achievements and chat on the right'],
  ['b', '16:9', '16:9 game; achievements and chat on the right, camera below'],
  ['c', 'CAMERA', 'a big camera column on the left, the 4:3 game on the right'],
  ['v', '9:16', 'vertical, for Shorts: camera, game, achievements']];
const STREAM_ACH = [
  ['auto', 'AUTO', 'a list while it fits, the grid when it does not'],
  ['list', 'LIST', 'one line per achievement'],
  ['grid', 'GRID', 'badges only']];

const OV_ROW = 36;          /* list line height, px */
const OV_HEAD = 30;         /* section heading height, px */
const IN_OBS = !!window.obsstudio;

/* Sets a stream choice: kept in this browser and written into the
   address, so the address can go into an OBS browser source. */
function setStreamOpt(key, param, val) {
  setPref(key, val);
  try {
    const q = new URLSearchParams(location.search);
    q.set(param, val);
    history.replaceState(null, '', location.pathname + '?' + q.toString() + location.hash);
  } catch (e) { /* no history on a file: page */ }
}

function overlayOn() { return !!OV_LAYOUTS[S.streamLayout.value]; }

/* Rounded rectangle as an SVG path. */
function ovRect([x, y, w, h], r) {
  if (!r) return `M${x} ${y}H${x + w}V${y + h}H${x}Z`;
  return `M${x + r} ${y}H${x + w - r}A${r} ${r} 0 0 1 ${x + w} ${y + r}V${y + h - r}A${r} ${r} 0 0 1 ${x + w - r} ${y + h}` +
         `H${x + r}A${r} ${r} 0 0 1 ${x} ${y + h - r}V${y + r}A${r} ${r} 0 0 1 ${x + r} ${y}Z`;
}

/* Corner ticks just outside a cut-out. */
function ovTicks([x, y, w, h]) {
  const o = 7, l = 18;
  return `M${x - o} ${y - o + l}V${y - o}H${x - o + l}M${x + w + o - l} ${y - o}H${x + w + o}V${y - o + l}` +
         `M${x + w + o} ${y + h + o - l}V${y + h + o}H${x + w + o - l}M${x - o + l} ${y + h + o}H${x - o}V${y + h + o - l}`;
}

function ovBox(r) { return { left: r[0] + 'px', top: r[1] + 'px', width: r[2] + 'px', height: r[3] + 'px' }; }

/* The panel: one surface with the cut-outs, their edges and ticks, and
   the rule over the status line. */
function OvShell({ L }) {
  const [W, H] = L.size;
  const r = S.theme.value === '8bit' ? 0 : 10;
  const holes = OV_HOLES.map(([k]) => L[k]).filter(Boolean);
  const d = `M0 0H${W}V${H}H0Z` + holes.map(b => ovRect(b, r)).join('');
  return html`<svg class="ov-shell" viewBox=${`0 0 ${W} ${H}`} aria-hidden="true">
    <defs>
      <linearGradient id="ov-base" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0" class="ov-s0" /><stop offset=".55" class="ov-s1" /><stop offset="1" class="ov-s2" />
      </linearGradient>
      <radialGradient id="ov-glow" cx=".08" cy="-.1" r=".75">
        <stop offset="0" class="ov-acc" stop-opacity=".22" /><stop offset="1" class="ov-acc" stop-opacity="0" />
      </radialGradient>
      <pattern id="ov-grid" width="48" height="48" patternUnits="userSpaceOnUse">
        <path d="M48 0H0V48" fill="none" stroke="#fff" stroke-opacity=".03" />
      </pattern>
    </defs>
    ${['url(#ov-base)', 'url(#ov-glow)', 'url(#ov-grid)'].map(f => html`<path key=${f} d=${d} fill=${f} fill-rule="evenodd" />`)}
    ${holes.map((b, i) => html`<g key=${i}>
      <path class="ov-lip-dark" d=${ovRect([b[0] - 3, b[1] - 3, b[2] + 6, b[3] + 6], r ? r + 3 : 0)} />
      <path class="ov-lip" d=${ovRect(b, r)} />
      <path class="ov-tick" d=${ovTicks(b)} />
    </g>`)}
    <path class="ov-rule" d=${`M20 ${L.status[1] - 7}H${W - 20}`} />
  </svg>`;
}

/* Moves a track up and down inside its viewport while it overflows,
   with a pause at each end; the wheel moves it by hand. `reset` sends
   it back to the top when it changes. */
function useRide(track, reset) {
  const st = useRef({ pos: 0, dir: 1, hold: 0, over: 0, last: 0 });

  const measure = toTop => {
    const tr = track.current;
    if (!tr) return;
    const s = st.current;
    s.over = Math.max(0, tr.scrollHeight - tr.parentElement.clientHeight);
    if (toTop) { s.pos = 0; s.dir = 1; s.hold = performance.now() + 2500; }
    s.pos = Math.min(s.pos, s.over);
    tr.style.transform = `translateY(${-s.pos}px)`;
  };

  useEffect(() => measure(true), [reset]);
  useEffect(() => measure(false));

  useEffect(() => {
    let raf = 0;
    const loop = now => {
      const s = st.current, tr = track.current;
      const dt = s.last ? Math.min(0.1, (now - s.last) / 1000) : 0;
      s.last = now;
      if (tr && s.over > 4 && now >= s.hold) {
        s.pos += s.dir * 32 * dt;
        if (s.pos >= s.over) { s.pos = s.over; s.dir = -1; s.hold = now + 2500; }
        if (s.pos <= 0) { s.pos = 0; s.dir = 1; s.hold = now + 2500; }
        tr.style.transform = `translateY(${-s.pos}px)`;
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);

    const vp = track.current && track.current.parentElement;
    const wheel = e => {
      const s = st.current;
      e.preventDefault();
      s.pos = Math.min(s.over, Math.max(0, s.pos + e.deltaY));
      s.hold = performance.now() + 5000;
      track.current.style.transform = `translateY(${-s.pos}px)`;
    };
    if (vp) vp.addEventListener('wheel', wheel, { passive: false });
    return () => { cancelAnimationFrame(raf); if (vp) vp.removeEventListener('wheel', wheel); };
  }, []);
}

/* State of one achievement for the overlay: done, near, na or locked. */
function ovState(a, lv) {
  if (a.state === 2) return 'done';
  if (a.state === 3) return 'na';
  return lv.p > 0 ? 'near' : 'locked';
}

/* The achievements: started ones first, then locked, then unlocked, as
   a list or a grid that rides when it does not fit. */
function OvAch({ s, m, box, layout }) {
  const pick = STREAM_ACH.some(([k]) => k === S.streamAch.value) ? S.streamAch.value : 'auto';
  const rows = m ? m.list.map(a => ({ a, lv: liveVal(a.id).value })) : [];
  const rank = x => { const st = ovState(x.a, x.lv); return st === 'near' ? 0 : st === 'done' ? 3 : st === 'na' ? 2 : 1; };
  rows.sort((x, y) => rank(x) - rank(y));
  const view = pick === 'auto' ? (rows.length * OV_ROW > box[3] - OV_HEAD ? 'grid' : 'list') : pick;
  const track = useRef();
  useRide(track, layout + view + (m ? s.game.id : 0));

  const item = ({ a, lv }) => {
    const st = ovState(a, lv);
    const mini = st === 'near' && html`<span class="ov-mini"><i style=${{ width: lv.p + '%' }} /></span>`;
    if (view === 'grid')
      return html`<span key=${a.id} class=${'ov-tile ' + st} title=${a.title}>
        <img src=${badgeUrl(a, st !== 'done')} alt="" loading="lazy" />${mini}</span>`;
    return html`<div key=${a.id} class=${'ov-item ' + st}>
      <img src=${badgeUrl(a, st !== 'done')} alt="" loading="lazy" />
      <div class="tx"><span class="t">${a.title}</span><span class="d">${lv.m || a.description}</span></div>
      <span class="pts">${a.points}</span>${mini}
    </div>`;
  };

  return html`<section class=${'ov-sec ov-ach v-' + view} style=${ovBox(box)}>
    <div class="ov-hd"><b>●</b>${t('ACHIEVEMENTS')}<span class="rule" /><span class="n">${m ? m.done + ' / ' + m.list.length : ''}</span></div>
    <div class="ov-vp"><div class="ov-track" ref=${track}>
      ${rows.length ? rows.map(item) : html`<div class="ov-none">${noGameText(s)}</div>`}
    </div></div>
  </section>`;
}

/* The game's head: cover, title, rich presence, progress. */
function OvHead({ s, m, box, stack }) {
  const f = s.follow || {};
  const rp = s.game.rp || (f.active ? f.rp : '');
  const n = m ? m.list.length : 0;
  return html`<section class=${'ov-sec ov-head' + (stack ? ' stack' : '')} style=${ovBox(box)}>
    ${m && m.icon ? html`<img class="ov-cover" src=${MEDIA + m.icon} alt="" />` : html`<span class="ov-cover" />`}
    <div class="ov-title">
      <h1>${s.game.title || s.game.serial || 'xerabora'}</h1>
      ${rp && html`<div class="ov-rp" key=${rp}><span>${rp}</span></div>`}
    </div>
    ${m && html`<${Fragment}>
      <span class="ov-vr" />
      <div class="ov-prog">
        <div class="nums"><span class="big">${m.done}<small> / ${n}</small></span>
          <span class="k"><span class="gold">${m.ptsDone}</span> / ${t('{points} pts', { points: m.ptsAll })}</span></div>
        <div class="ov-bar"><i style=${{ width: pct(m.done, n) + '%' }} /></div>
      </div>
    <//>`}
  </section>`;
}

/* One line along the bottom: the source and its numbers, the brand. */
function OvStatus({ s, box, narrow }) {
  const c = s.console || {}, f = s.follow || {};
  const sep = html`<i class="ov-dot" />`;
  let parts;
  if (c.connected) {
    const w = c.watch || {};
    parts = [
      html`<span class="src">${t('CONSOLE')}</span>`,
      html`<b>${c.ip}</b>`,
      s.game.serial ? html`<b>${s.game.serial}</b>` : null,
      html`<span>${t('{n} snapshots/s', { n: fast.rate.value })}</span>`,
      html`<span class=${fast.loss.value > 0.02 ? 'warn' : 'ok'}>${t('lost {pct}', { pct: (fast.loss.value * 100).toFixed(1) + '%' })}</span>`,
      !narrow && w.addresses ? html`<span>${t('watch {addresses} addr · {kb} KB', { addresses: w.addresses, kb: (w.bytes / 1024).toFixed(1) })}</span>` : null,
      fast.seconds.value ? html`<span>${t('session {time}', { time: hms(fast.seconds.value) })}</span>` : null];
  } else if (f.active) {
    parts = [html`<span class="src web">${t('EMULATOR')}</span>`, s.game.console ? html`<b>${s.game.console}</b>` : null,
             html`<span>${t('via RetroAchievements')}</span>`];
  } else {
    parts = [html`<span>${noGameText(s)}</span>`];
  }
  const on = !!(c.connected || f.active);
  return html`<section class="ov-sec ov-status" style=${ovBox(box)}>
    <span class=${'ov-live' + (on ? ' on' : '')}><i />${on ? t('LIVE') : t('OFFLINE')}</span>
    ${parts.filter(Boolean).map((p, i) => html`${i ? sep : ''}${p}`)}
    <span class="sp" />
    ${!narrow && html`<span class="ov-brand">xe<span class="r">R</span><span class="a">A</span>bora</span>`}
  </section>`;
}

/* The unlock card over the game cut-out. */
function OvToast({ s, game }) {
  const u = S.toast.value;
  if (!u || !game) return null;
  const a = (s.game.achievements || []).find(x => x.id === u.id);
  return html`<div class="ov-toast-at" style=${{ left: game[0] + game[2] / 2 + 'px', top: game[1] + 36 + 'px' }}>
    <div class="ov-toast" key=${u.id}>
      <img src=${BADGE + u.badge + '.png'} alt="" />
      <div><div class="k">${t('ACHIEVEMENT UNLOCKED')}</div><div class="t">${u.title}</div>
        ${a && a.description && html`<div class="d">${a.description}</div>`}</div>
      <div class="p">+${u.points}</div>
    </div>
  </div>`;
}

/* The layout and view keys; shown while the mouse moves, never in OBS. */
function StreamPicker({ overlay }) {
  const lay = S.streamLayout.value, ach = S.streamAch.value;
  const chips = (items, on, key, param) => items.map(([k, label, tip]) => html`<${Chip} key=${k} on=${on === k}
    title=${t(tip)} onClick=${() => setStreamOpt(key, param, k)}>${t(label)}<//>`);
  return html`<div class="spick">
    <span class="k">${t('LAYOUT')}</span>${chips(STREAM_LAYOUTS, lay, 'streamLayout', 'layout')}
    ${overlay && html`<span class="k">${t('ACHIEVEMENTS')}</span>${chips(STREAM_ACH, ach, 'streamAch', 'ach')}`}
  </div>`;
}

function OvControls() {
  const [shown, setShown] = useState(true);
  useEffect(() => {
    let id = setTimeout(() => setShown(false), 3000);
    const move = () => { setShown(true); clearTimeout(id); id = setTimeout(() => setShown(false), 3000); };
    window.addEventListener('mousemove', move);
    return () => { clearTimeout(id); window.removeEventListener('mousemove', move); };
  }, []);
  const [W, H] = OV_LAYOUTS[S.streamLayout.value].size;
  return html`<div class=${'ov-ctl' + (shown ? ' on' : '')}>
    <a class="brand" href="#" onClick=${e => { e.preventDefault(); go('tabs'); }}><span>xe<span class="r">R</span><span class="a">A</span>bora</span></a>
    <${StreamPicker} overlay />
    <span class="obs">${t('OBS: browser source with this address, {w}×{h}', { w: W, h: H })}</span>
    <${Btn} sm onClick=${() => go('tabs')}>${t('BACK TO TABS')}<//>
  </div>`;
}

/* Scale that fits a W×H stage into the window. */
function useFit(W, H) {
  const calc = () => Math.min(window.innerWidth / W, window.innerHeight / H);
  const [k, setK] = useState(calc);
  useEffect(() => {
    const on = () => setK(calc());
    on();
    window.addEventListener('resize', on);
    return () => window.removeEventListener('resize', on);
  }, [W, H]);
  return k;
}

function OverlayView() {
  const key = S.streamLayout.value;
  const L = OV_LAYOUTS[key];
  const [W, H] = L.size;
  const k = useFit(W, H);
  useEffect(() => {
    document.documentElement.classList.add('ov-on');
    return () => document.documentElement.classList.remove('ov-on');
  }, []);

  const s = S.state.value;
  const m = s && (s.game.achievements || []).length ? liveModel(s) : null;
  return html`<div class="ov">
    <div class="ov-stage" style=${{ width: W + 'px', height: H + 'px', transform: `translate(-50%, -50%) scale(${k})` }}>
      <${OvShell} L=${L} />
      ${!IN_OBS && OV_HOLES.map(([h, label]) => L[h] && html`<span key=${h} class="ov-hole" style=${ovBox(L[h])}>${t(label)}</span>`)}
      ${s && html`<${Fragment}>
        ${L.ach && html`<${OvAch} s=${s} m=${m} box=${L.ach} layout=${key} />`}
        ${L.head && html`<${OvHead} s=${s} m=${m} box=${L.head} stack=${L.stack} />`}
        <${OvStatus} s=${s} box=${L.status} narrow=${L.narrow} />
        ${L.foot && html`<section class="ov-sec ov-foot" style=${ovBox(L.foot)}>
          <div class="ov-brand">xe<span class="r">R</span><span class="a">A</span>bora</div>
          <div class="k">${t('RetroAchievements on a real PlayStation 2')}</div></section>`}
        <${OvToast} s=${s} game=${L.game} />
      <//>`}
    </div>
    ${!IN_OBS && html`<${OvControls} />`}
  </div>`;
}
