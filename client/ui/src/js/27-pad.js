/* ---- PAD: a DualShock 2 on the page, sent to the console over UDP ------ */

/* Button bits in libpad order; a set bit is a pressed button. */
const PAD_BITS = {
  select: 0x0001, l3: 0x0002, r3: 0x0004, start: 0x0008,
  up: 0x0010, right: 0x0020, down: 0x0040, left: 0x0080,
  l2: 0x0100, r2: 0x0200, l1: 0x0400, r1: 0x0800,
  triangle: 0x1000, circle: 0x2000, cross: 0x4000, square: 0x8000,
};

/* On-screen buttons: name, label, centre in percent of the drawing, shape. */
const PAD_KEYS = [
  ['l2', 'L2', 20.5, 3.8, 'sh'], ['l1', 'L1', 20.5, 11.6, 'bump'],
  ['r2', 'R2', 79.6, 3.8, 'sh'], ['r1', 'R1', 79.6, 11.6, 'bump'],
  ['up', '▲', 20.4, 37.5, 'dp'], ['down', '▼', 20.4, 53.3, 'dp'],
  ['left', '◀', 15.2, 45.3, 'dp'], ['right', '▶', 25.7, 45.3, 'dp'],
  ['triangle', '△', 79.7, 33.3, 'face'], ['cross', '✕', 79.7, 57.1, 'face'],
  ['square', '□', 72.0, 45.3, 'face'], ['circle', '○', 87.4, 45.3, 'face'],
  ['select', 'SELECT', 41.1, 43.6, 'opt'], ['start', 'START', 59.0, 43.6, 'opt'],
  ['l3', 'L3', 34.8, 89.5, 'mid'], ['r3', 'R3', 65.1, 89.5, 'mid'],
];

/* Keyboard: event.code to button. */
const PAD_KEYBOARD = {
  ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right',
  KeyZ: 'cross', KeyX: 'circle', KeyA: 'square', KeyS: 'triangle',
  KeyQ: 'l1', KeyW: 'r1', Digit1: 'l2', Digit2: 'r2',
  Enter: 'start', Backspace: 'select', KeyC: 'l3', KeyV: 'r3',
};

/* Browser Gamepad API, standard mapping: button index to pad button. */
const PAD_GAMEPAD = ['cross', 'circle', 'square', 'triangle', 'l1', 'r1', 'l2', 'r2',
  'select', 'start', 'l3', 'r3', 'up', 'down', 'left', 'right'];

/* Converts a stick axis from -1..1 to the pad's 0..255, 0x80 at rest. */
function padAxis(v) {
  if (Math.abs(v) < 0.12) return 0x80;
  return Math.max(0, Math.min(255, Math.round((v + 1) * 127.5)));
}

/* One analog stick: drag the knob, it springs back on release. */
function PadStick({ x, y, onMove }) {
  const [pos, setPos] = useState([0, 0]);
  const base = useRef();

  const move = e => {
    const r = base.current.getBoundingClientRect();
    let dx = (e.clientX - r.left - r.width / 2) / (r.width / 2);
    let dy = (e.clientY - r.top - r.height / 2) / (r.height / 2);
    const len = Math.hypot(dx, dy);
    if (len > 1) { dx /= len; dy /= len; }
    setPos([dx, dy]);
    onMove(dx, dy);
  };
  const down = e => { e.currentTarget.setPointerCapture(e.pointerId); move(e); };
  const up = () => { setPos([0, 0]); onMove(0, 0); };

  return html`<div class="pad-stick" ref=${base} style=${{ left: x + '%', top: y + '%' }}
    onPointerDown=${down} onPointerMove=${e => e.buttons && move(e)} onPointerUp=${up} onPointerCancel=${up}>
    <i style=${{ transform: `translate(${pos[0] * 60}%, ${pos[1] * 60}%)` }} />
  </div>`;
}

function PadTab() {
  /* Each source keeps its own state; what goes out is their union. */
  const src = useRef({ screen: 0, keys: 0, gp: 0, stick: [0x80, 0x80, 0x80, 0x80], gpAxes: null });
  const net = useRef({ seq: 0, busy: false, dirty: false, last: '' });
  const [shown, setShown] = useState({ b: 0, axes: [0x80, 0x80, 0x80, 0x80] });
  const [stats, setStats] = useState({ delivered: null, gamepad: '' });

  const current = () => {
    const s = src.current;
    return { b: s.screen | s.keys | s.gp, axes: s.gpAxes || s.stick };
  };

  /* Posts the current state; a change during a post is sent right after. */
  const send = async force => {
    const n = net.current;
    const cur = current();
    const key = cur.b + ':' + cur.axes.join(',');
    if (!force && key === n.last) return;
    if (n.busy) { n.dirty = true; return; }
    n.busy = true;
    n.last = key;
    n.seq = (n.seq + 1) >>> 0;
    try {
      const r = await postForm('/pad', { seq: n.seq, b: cur.b, lx: cur.axes[0], ly: cur.axes[1], rx: cur.axes[2], ry: cur.axes[3] });
      setStats(st => ({ ...st, delivered: !!r.sent }));
    } catch (e) {
      setStats(st => ({ ...st, delivered: false }));
    }
    n.busy = false;
    if (n.dirty) { n.dirty = false; send(false); }
  };

  const changed = () => { setShown(current()); send(false); };

  const press = (name, on) => {
    const s = src.current;
    s.screen = on ? s.screen | PAD_BITS[name] : s.screen & ~PAD_BITS[name];
    changed();
  };

  const stick = side => (dx, dy) => {
    const s = src.current;
    const i = side === 'l' ? 0 : 2;
    s.stick = s.stick.slice();
    s.stick[i] = padAxis(dx);
    s.stick[i + 1] = padAxis(dy);
    changed();
  };

  useEffect(() => {
    /* Keyboard, while this tab is open and no field has the focus. */
    const key = on => e => {
      const name = PAD_KEYBOARD[e.code];
      if (!name || /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName)) return;
      e.preventDefault();
      const s = src.current;
      s.keys = on ? s.keys | PAD_BITS[name] : s.keys & ~PAD_BITS[name];
      changed();
    };
    const kd = key(true), ku = key(false);
    window.addEventListener('keydown', kd);
    window.addEventListener('keyup', ku);

    /* A window that loses the focus never sees the key go up. */
    const drop = () => { src.current.keys = 0; src.current.screen = 0; changed(); };
    window.addEventListener('blur', drop);
    document.addEventListener('visibilitychange', drop);

    /* Gamepads: polled once per animation frame. */
    let raf = 0;
    let gpName = '';
    const poll = () => {
      const s = src.current;
      const pads = navigator.getGamepads ? Array.from(navigator.getGamepads()).filter(Boolean) : [];
      const gp = pads[0];
      let b = 0, axes = null;
      if (gp) {
        PAD_GAMEPAD.forEach((name, i) => { if (gp.buttons[i] && gp.buttons[i].pressed) b |= PAD_BITS[name]; });
        if (gp.axes.length >= 4) axes = [padAxis(gp.axes[0]), padAxis(gp.axes[1]), padAxis(gp.axes[2]), padAxis(gp.axes[3])];
        if (axes && axes.every(v => v === 0x80)) axes = null;
      }
      if ((gp ? gp.id : '') !== gpName) {
        gpName = gp ? gp.id : '';
        setStats(st => ({ ...st, gamepad: gpName }));
      }
      if (b !== s.gp || String(axes) !== String(s.gpAxes)) {
        s.gp = b;
        s.gpAxes = axes;
        changed();
      }
      raf = requestAnimationFrame(poll);
    };
    raf = requestAnimationFrame(poll);

    /* Heartbeat: the state again every 250 ms, so a lost datagram heals
       and the console can release the buttons when the page goes away. */
    const beat = setInterval(() => send(true), 250);

    return () => {
      window.removeEventListener('keydown', kd);
      window.removeEventListener('keyup', ku);
      window.removeEventListener('blur', drop);
      document.removeEventListener('visibilitychange', drop);
      cancelAnimationFrame(raf);
      clearInterval(beat);
      src.current = { screen: 0, keys: 0, gp: 0, stick: [0x80, 0x80, 0x80, 0x80], gpAxes: null };
      send(true);
    };
  }, []);

  const s = S.state.value;
  const c = (s && s.console) || {};

  return html`<${Fragment}>
    <${Eyebrow}>${t('NETWORK GAMEPAD &middot; EXPERIMENTAL')}<//>
    <div class="pad-status">
      ${c.connected ? t('buttons go to the console at {ip}', { ip: c.ip })
        : stats.delivered ? t('buttons go to the console')
                    : html`<span class="warn">${t("no console yet: turn on RA network gamepad in OPL's network settings and stay on the same network")}</span>`}
    </div>
    <div class="pad" onContextMenu=${e => e.preventDefault()}>
      <div class="pad-art" dangerouslySetInnerHTML=${{ __html: PAD_ART }} />
      ${PAD_KEYS.map(([name, label, x, y, shape]) => html`<button key=${name} type="button"
        class=${'pad-btn ' + shape + (shown.b & PAD_BITS[name] ? ' on' : '')}
        style=${{ left: x + '%', top: y + '%' }}
        onPointerDown=${e => { e.currentTarget.setPointerCapture(e.pointerId); press(name, true); }}
        onPointerUp=${() => press(name, false)} onPointerCancel=${() => press(name, false)}>${label}</button>`)}
      <${PadStick} x=${34.8} y=${67.6} onMove=${stick('l')} />
      <${PadStick} x=${65.1} y=${67.6} onMove=${stick('r')} />
    </div>
    <div class="pad-readout">
      ${stats.delivered === false && html`<span class="warn">${t('not delivered')}</span>`}
    </div>
    <div class="pad-help">
      ${stats.gamepad ? t('gamepad: {name}', { name: stats.gamepad }) : t('connect a gamepad and press any button on it; the browser shows it only after a press')}
      <br />${t('keyboard: arrows, Z ✕, X ○, A □, S △, Q L1, W R1, 1 L2, 2 R2, Enter START, Backspace SELECT, C L3, V R3')}
    </div>
  <//>`;
}
