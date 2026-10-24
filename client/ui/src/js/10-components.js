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

/* Line icons, 24-unit grid, stroked with the current colour. */
const ICONS = {
  'all': 'M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z',
  'lock': 'M6 11V8a6 6 0 0 1 12 0v3M5 11h14v10H5zM12 15v3',
  'unlock': 'M6 11V8a6 6 0 0 1 11.5-2.3M5 11h14v10H5zM12 15v3',
  'warn': 'M12 3 2.5 20h19zM12 10v4M12 17v.5',
  'flag': 'M5 21V4M5 4h12l-3 4 3 4H5',
  'trophy': 'M8 4h8v5a4 4 0 0 1-8 0zM8 6H4a4 4 0 0 0 4 4M16 6h4a4 4 0 0 1-4 4M12 13v4M8 21h8M9 17h6',
  'list-num': 'M4 6h2M4 12h2M4 18h2M9 6h11M9 12h11M9 18h11',
  'route': 'M4 19h5a3 3 0 0 0 0-6H9a3 3 0 0 1 0-6h11M17 4l3 3-3 3',
  'clock': 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2',
  'gem': 'M7 3h10l4 6-9 12L3 9zM3 9h18M9 9l3 12M15 9l-3 12',
  'rows': 'M4 6h16M4 12h16M4 18h16',
  'grid': 'M4 4h4v4H4zM10 4h4v4h-4zM16 4h4v4h-4zM4 10h4v4H4zM10 10h4v4h-4zM16 10h4v4h-4zM4 16h4v4H4zM10 16h4v4h-4zM16 16h4v4h-4z',
  'sliders': 'M4 7h10M18 7h2M14 5v4M4 17h4M12 17h8M8 15v4M4 12h16',
  'close': 'M6 6l12 12M18 6 6 18',
  'chevron': 'M6 9l6 6 6-6'
};
function Icon({ name }) {
  return html`<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d=${ICONS[name] || ''} /></svg>`;
}

/* The filter bar: a button that opens the drawer, the current filter
   and order, LIST / GRID. */
function ToolsBar({ filter, sort, view, onFilter, onSort, onView }) {
  const [open, setOpen] = useState(false);
  const f = ACH_FILTERS.find(x => x[0] === filter) || ACH_FILTERS[0];
  const o = ACH_SORTS.find(x => x[0] === sort) || ACH_SORTS[0];
  const changed = filter !== 'all' || sort !== 'author';
  const tip = x => t(x[1]) + ' — ' + t(x[3]);
  return html`<div class="tbar">
    <button type="button" class=${'ichip sm wide' + (changed ? ' on' : '')} onClick=${() => setOpen(true)}
      title=${t('filter and order')}><${Icon} name="sliders" /><span>${t('FILTER')}</span></button>
    <span class="cur" title=${tip(f)}><${Icon} name=${f[2]} /><span>${t(f[1])}</span></span>
    <span class="cur" title=${tip(o)}><${Icon} name=${o[2]} /><span>${t(o[1])}</span></span>
    <span class="sp"></span>
    ${ACH_VIEWS.map(x => html`<button key=${x[0]} type="button" class=${'ichip sm' + (view === x[0] ? ' on' : '')}
      title=${tip(x)} aria-label=${t(x[1])} onClick=${() => onView(x[0])}><${Icon} name=${x[2]} /></button>`)}
    ${open && html`<${Drawer} filter=${filter} sort=${sort} onFilter=${onFilter} onSort=${onSort} onClose=${() => setOpen(false)} />`}
  </div>`;
}

/* The drawer with the filters and the orders; closes on Esc, a click
   outside or the cross. */
function Drawer({ filter, sort, onFilter, onSort, onClose }) {
  useEffect(() => {
    const k = e => { if (e.key === 'Escape') onClose(); };
    window.addEventListener('keydown', k);
    return () => window.removeEventListener('keydown', k);
  }, []);
  const keys = (items, on, pick) => items.map(([k, label, icon, text]) => html`<button key=${k} type="button"
    class=${'dkey' + (k === on ? ' on' : '')} onClick=${() => pick(k)}>
    <${Icon} name=${icon} /><span class="l">${t(label)}</span><span class="d">${t(text)}</span></button>`);
  return html`<${Fragment}>
    <div class="drawer-back" onClick=${onClose} />
    <aside class="drawer" role="dialog" aria-label=${t('filter and order')}>
      <div class="dhead"><span>${t('FILTER')}</span>
        <button type="button" class="ichip sm" onClick=${onClose} title=${t('CLOSE')} aria-label=${t('CLOSE')}><${Icon} name="close" /></button></div>
      ${keys(ACH_FILTERS, filter, onFilter)}
      <div class="dhead"><span>${t('ORDER')}</span></div>
      ${keys(ACH_SORTS, sort, onSort)}
    </aside>
  <//>`;
}

/* Achievement row: badge, title, description, a right column of
   lines. `state` picks the look: done, near, na, locked. */
function AchRow({ a, state, big, right, below, mode, open, onClick }) {
  const locked = state !== 'done';
  return html`<div class=${'row ' + state + (big ? ' big' : '') + (mode ? ' m' + mode : '') + (onClick ? ' clickable' : '') + (open ? ' open' : '')}
      onClick=${onClick}>
    <img class="badge" src=${badgeUrl(a, locked)} alt="" loading="lazy" />
    <div class="body">
      <div class="t">${a.title}</div>
      <div class="d">${a.description}</div>
      ${below}
    </div>
    <div class="m">${right}</div>
  </div>
  ${open && html`<${Comments} id=${a.id} />`}`;
}

/* The players' comments under an achievement row. */
function Comments({ id }) {
  S.commentsTick.value;
  const c = S.commentsCache[id];
  const list = c && c.comments ? c.comments : [];
  return html`<div class="comments" onClick=${e => e.stopPropagation()}>
    <div class="k">${t('COMMENTS')}
      <a href=${'https://retroachievements.org/achievement/' + id} target="_blank" rel="noopener">${t('on retroachievements.org')}</a></div>
    ${!c || c.loading ? html`<div class="empty">${t('loading...')}</div>`
      : c.error ? html`<div class="empty">${t('could not reach the client')}</div>`
      : !list.length ? html`<div class="empty">${t('no player comments yet')}</div>`
      : list.slice(-12).reverse().map((m, i) => html`<div class="cm" key=${i}>
          <div class="who"><b>${m.user}</b> <span class="mute">${m.date}</span></div>
          <div class="txt">${m.text}</div>
        </div>`)}
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
   knocks the speed down for a moment. */
function Disc({ src }) {
  const face = useRef();
  const disc = useRef();

  useEffect(() => {
    const still = matchMedia('(prefers-reduced-motion: reduce)').matches;
    if (still) return undefined;
    let angle = 0, last = performance.now(), raf = 0;
    let gaps = fast.gaps.peek(), kick = 0;

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
   still cover otherwise. */
function Cover({ src }) {
  if (mode.value === 'console') return html`<${Disc} src=${src} />`;
  return src ? html`<img class="cover" src=${src} alt="" />` : html`<div class="cover" />`;
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
