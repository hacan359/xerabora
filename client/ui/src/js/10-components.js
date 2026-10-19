/* ---- Shared components ------------------------------------------------- */

/* Section label with a rule after it; `count` is an optional tail. */
function Eyebrow({ children, count }) {
  return html`<h2 class="eyebrow">${children}${count != null && html`<span class="count">${count}</span>`}</h2>`;
}

/* One stat tile. `v` may be a signal: the text then updates in place. */
function Cell({ k, v, cls }) {
  return html`<div class="cell"><div class="k">${k}</div><div class=${'v ' + (cls || '')}>${v}</div></div>`;
}

function Bar({ value, thin }) {
  return html`<div class=${'bar' + (thin ? ' thin' : '')}><i style=${{ width: pctClamp(value) + '%' }} /></div>`;
}

/* Softcore and hardcore progress, one bar each. `labels` adds the
   name and the count beside each bar. */
function Bars({ awarded, hardcore, total, thin, labels }) {
  const row = (cls, label, n) => html`<div class="prog">
    ${labels && html`<span class="k">${label}</span>`}
    <div class=${'bar ' + cls + (thin ? ' thin' : '')}><i style=${{ width: pct(n, total) + '%' }} /></div>
    ${labels && html`<span class="n">${n} / ${total}</span>`}
  </div>`;
  return html`<div class="bars">${row('sc', t('SOFTCORE'), awarded || 0)}${row('hc', t('HARDCORE'), hardcore || 0)}</div>`;
}

/* Measured progress of one achievement, read from its live signal. */
function Mini({ id }) {
  const v = liveVal(id).value;
  return v.p > 0 ? html`<div class="mini"><i style=${{ width: v.p + '%' }} /></div>` : null;
}

function Measured({ id }) {
  const v = liveVal(id).value;
  return v.m ? html`<span class="lead acc">${v.m}</span>` : null;
}

function Chip({ on, onClick, children, n, cls, title }) {
  return html`<span class=${'chip' + (on ? ' on' : '') + (cls ? ' ' + cls : '')} onClick=${onClick} title=${title}>
    ${children}${n != null && html` <span class="n">${n}</span>`}</span>`;
}

function Empty({ children }) { return html`<div class="empty">${children}</div>`; }

/* Achievement row: badge, title, description, a right column of
   lines. `state` picks the look: done, near, na, locked. */
function AchRow({ a, state, big, right, below, mode }) {
  const locked = state !== 'done';
  return html`<div class=${'row ' + state + (big ? ' big' : '') + (mode ? ' m' + mode : '')}>
    <img class="badge" src=${badgeUrl(a, locked)} alt="" loading="lazy" />
    <div class="body">
      <div class="t">${a.title}</div>
      <div class="d">${a.description}</div>
      ${below}
    </div>
    <div class="m">${right}</div>
  </div>`;
}

/* Type tag for the right column: missable in amber, the rest muted. */
function TypeTag({ a }) {
  const ty = achType(a);
  if (!ty) return null;
  return html`<span class=${ty === 'missable' ? 'warn' : ''}>${t(ty.replace('_', ' '))}</span>`;
}

/* Points and the tags after them, one line. */
function Pts({ a, extra }) {
  const parts = [html`<span class="gold">${t('{points} pts', { points: a.points })}</span>`];
  const tag = achType(a);
  if (tag) parts.push(html`<${TypeTag} a=${a} />`);
  if (extra) parts.push(...(Array.isArray(extra) ? extra : [extra]));
  return html`<span class="tag">${parts.map((p, i) => html`${i ? ' · ' : ''}${p}`)}</span>`;
}

/* Labelled toggle; `busy` greys it while the client answers. */
function Switch({ on, label, text, busy, onToggle }) {
  return html`<div class=${'switch' + (on ? ' on' : '') + (busy ? ' busy' : '')} onClick=${onToggle}>
    <span class="k">${label}</span>
    <span class="d">${text}</span>
    <span class="tog" />
  </div>`;
}

function Btn({ children, onClick, primary, danger, sm, disabled, type, id }) {
  const cls = 'btn' + (primary ? ' primary' : '') + (danger ? ' danger' : '') + (sm ? ' sm' : '');
  return html`<button class=${cls} onClick=${onClick} disabled=${disabled} type=${type || 'button'} id=${id}>${children}</button>`;
}

/* The disc: the cover turned by a requestAnimationFrame loop. Speed
   follows the snapshot rate; the loss share slows it; each new gap
   knocks the speed down for a moment and lights the hole red. */
function Disc({ src }) {
  const face = useRef();
  const disc = useRef();

  useEffect(() => {
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (still) return undefined;
    let angle = 0, last = performance.now(), raf = 0;
    let gaps = fast.gaps.peek(), kick = 0, stalled = false;

    const loop = now => {
      const dt = Math.min(0.1, (now - last) / 1000);
      last = now;

      const g = fast.gaps.peek();
      if (g > gaps) kick = Math.min(1, kick + 0.6);
      gaps = g;
      kick = Math.max(0, kick - dt * 1.6);

      const rate = fast.rate.peek();
      const base = rate > 0 ? 90 + rate * 3 : 25;
      const speed = base * (1 - 0.75 * fast.loss.peek()) * (1 - 0.92 * kick);
      angle = (angle + speed * dt) % 360;
      if (face.current) face.current.style.transform = 'rotate(' + angle.toFixed(2) + 'deg)';

      const stall = kick > 0.35;
      if (stall !== stalled && disc.current) {
        disc.current.classList.toggle('stall', stall);
        stalled = stall;
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  return html`<div class="disc" ref=${disc}>
    <div class="face" ref=${face}>
      ${src && html`<img src=${src} alt="" />`}
      <div class="iris" /><div class="grooves" />
    </div>
    <div class="sheen" /><div class="rim" /><div class="hole" />
  </div>`;
}

/* The cover of the game on LIVE: a disc while the console streams, a
   still cover with a signal ring while following, plain otherwise. */
function Cover({ src }) {
  const m = mode.value;
  if (m === 'console') return html`<${Disc} src=${src} />`;
  const img = src ? html`<img class="cover" src=${src} alt="" />` : html`<div class="cover" />`;
  if (m === 'follow') return html`<div class="cover-wrap">${img}<div class="ring" /></div>`;
  return img;
}

function Toast() {
  const u = S.toast.value;
  if (!u) return null;
  return html`<div class="toast">
    <img src=${BADGE + u.badge + '.png'} alt="" />
    <div>
      <div class="k">${t('ACHIEVEMENT UNLOCKED')}</div>
      <div class="t">${u.title}</div>
      <div class="p">${t('+{points} pts', { points: u.points })}</div>
    </div>
  </div>`;
}

/* One sweep of light, mounted for 1.3 s when the console connects. */
function PowerOn() {
  const n = S.powerOn.value;
  const [shown, setShown] = useState(0);

  useEffect(() => {
    if (!n) return undefined;
    setShown(n);
    const id = setTimeout(() => setShown(0), 1300);
    return () => clearTimeout(id);
  }, [n]);
  return shown ? html`<div class="poweron" key=${shown} />` : null;
}
