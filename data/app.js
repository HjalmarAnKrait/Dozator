'use strict';

// ─── Layout detection ─────────────────────────────────────────────────────
const params = new URLSearchParams(location.search);
const forcedLayout = params.get('layout');
const mode = forcedLayout || (window.innerWidth >= 900 ? 'desktop' : 'mobile');
document.body.dataset.layout = mode;

// ─── WebSocket ────────────────────────────────────────────────────────────
const WS_URL = `ws://${location.hostname}/ws`;
let ws = null;
let lastState = null;
let connAttempts = 0;
const BACKOFF = [500, 1000, 2000, 4000, 8000];

function connect() {
  setConnStatus('warn', 'переподключение...');
  ws = new WebSocket(WS_URL);
  ws.onopen    = () => { connAttempts = 0; setConnStatus('ok', 'соединение'); };
  ws.onclose   = () => { setConnStatus('err', 'нет связи'); scheduleReconnect(); };
  ws.onerror   = () => { ws.close(); };
  ws.onmessage = (e) => { try { handleMsg(JSON.parse(e.data)); } catch {} };
}

function scheduleReconnect() {
  const delay = BACKOFF[Math.min(connAttempts, BACKOFF.length - 1)];
  connAttempts++;
  setTimeout(connect, delay);
}

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

function setConnStatus(cls, text) {
  const dot  = document.getElementById('conn-dot');
  const span = document.getElementById('conn-text');
  dot.className  = cls;
  span.textContent = text;
}

// ─── Message handler ──────────────────────────────────────────────────────
function handleMsg(msg) {
  if (msg.type === 'state') {
    lastState = msg;
    render(msg);
  } else if (msg.type === 'log') {
    appendLog(msg.level, msg.msg);
  }
}

// ─── OLED text rendering ──────────────────────────────────────────────────
function esc(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

function pad(s, n) {
  s = String(s);
  while (s.length < n) s += ' ';
  return s.length > n ? s.slice(0, n) : s;
}

function fmtTime(sec) {
  const s = Math.max(0, Math.round(sec));
  return `${Math.floor(s/60)}:${String(s%60).padStart(2,'0')}`;
}

function fmtF1(v) { return parseFloat(v).toFixed(1); }
function fmtF0(v) { return parseFloat(v).toFixed(0); }

// Returns array of { text: string, cursor: bool, editing: bool, sep: bool }
function getLines(st) {
  const c   = st.cursor;
  const ed  = st.editing;
  const sw  = st.switches;
  const cy  = st.cycle;
  const dos = st.dosing;
  const ui  = st.ui;

  switch (st.screen) {
    case 'PARKING': return [
      line(''),
      line('     Парковка'),
      line('  Возврат в 0...'),
      line(''),
      line('      ^ ^ ^'),
      line(''),
    ];

    case 'PARKED': {
      const pA = cy.syringeA, pB = cy.syringeB;
      return [
        line(`A Vol: ${fmtF0(pA.volume).padStart(4)} мл`, c===0, c===0&&ed),
        line(`A D:  ${fmtF1(pA.diameter).padStart(5)} мм`,  c===1, c===1&&ed),
        line(`B Vol: ${fmtF0(pB.volume).padStart(4)} мл`, c===2, c===2&&ed),
        line(`B D:  ${fmtF1(pB.diameter).padStart(5)} мм`,  c===3, c===3&&ed),
        sep(),
        line('Старт зарядки', c===4),
        line('> Сервис',      c===5),
      ];
    }

    case 'CHARGING': return [
      line('     ЗАРЯДКА     '),
      line(''),
      line(`  A:[${sw.a?'✓':' '}]    B:[${sw.b?'✓':' '}]`),
      line(''),
      line('       v v v'),
      line(''),
      line('  Долгий клик = Назад', false, false, true),
    ];

    case 'CHARGED': {
      const tr = st.timeRange || {};
      return [
        line('      ГОТОВО'),
        line(`Время: ${fmtF1(cy.doseTimeMin)} мин`, c===0, c===0&&ed),
        line(`(${fmtF1(tr.min||0)}-${fmtF1(tr.max||0)} мин)`, false, false, true),
        line(`A: ${fmtF1(cy.flowA)} мл/мин`),
        line(`B: ${fmtF1(cy.flowB)} мл/мин`),
        line(c===1 ? '   [ ПУСК ]   ' : ' [ ПУСК ] ', c===1),
      ];
    }

    case 'DOSING': {
      const pct    = Math.round(dos.progress * 100);
      const barW   = 20;
      const filled = Math.round(barW * dos.progress);
      const bar    = '█'.repeat(filled) + '░'.repeat(barW - filled);
      return [
        line(`ДОЗИРОВАНИЕ    ${String(pct).padStart(3)}%`),
        line(bar),
        line(`${fmtTime(dos.elapsedSec)} -> ${fmtTime(dos.totalSec - dos.elapsedSec)}`),
        line(`A:${fmtF1(dos.volumeA)}/${fmtF0(dos.totalSec>0?dos.volumeA/Math.max(0.01,dos.progress):0)} мл`),
        line(`B:${fmtF1(dos.volumeB)}/${fmtF0(dos.totalSec>0?dos.volumeB/Math.max(0.01,dos.progress):0)} мл`),
        line('Long=СТОП', false, false, true),
      ];
    }

    case 'DONE': return [
      line('       КОНЕЦ'),
      line('   -- Завершено --'),
      line(''),
      line(`Время:    ${fmtTime(dos.elapsedSec)}`),
      line(`Подано A: ${fmtF0(dos.volumeA)} мл`),
      line(`Подано B: ${fmtF0(dos.volumeB)} мл`),
    ];

    case 'SERVICE_MENU': {
      const sets = st.settings;
      return [
        line('    > СЕРВИС'),
        line(`Шаг винта  ${fmtF1(sets.screwPitch)} мм`, c===0),
        line(`Шприцы  (${sets.presets.length})`,         c===1),
        line(`Сон экрана ${sets.sleepTimeout} с`,        c===2),
        line(''),
        line('Назад', c===3),
      ];
    }

    case 'SERVICE_PITCH': {
      const v = fmtF1(st.settings.screwPitch) + ' мм';
      return [
        line('    ШАГ ВИНТА'),
        line(''),
        line(v.padStart(10), true, true),
        line(''),
        line('диапазон 0.5-10 мм', false, false, true),
        line('^ изм  |  <- назад', false, false, true),
      ];
    }

    case 'SERVICE_SLEEP': {
      const v = st.settings.sleepTimeout + ' с';
      return [
        line('   СОН ЭКРАНА'),
        line(''),
        line(v.padStart(8), true, true),
        line(''),
        line('диапазон 5-300 с', false, false, true),
        line('^ изм  |  <- назад', false, false, true),
      ];
    }

    case 'SERVICE_SYRINGES': {
      const presets = st.settings.presets;
      const total   = presets.length + 2;
      const VISIBLE = 4;
      let scrollTop = Math.max(0, Math.min(c - VISIBLE + 1, total - VISIBLE));
      scrollTop = Math.max(0, scrollTop);
      const rows = [line(`  ШПРИЦЫ (${presets.length})`)];
      for (let row = 0; row < VISIBLE; row++) {
        const idx = scrollTop + row;
        if (idx >= total) { rows.push(line('')); continue; }
        let txt;
        if (idx < presets.length) {
          txt = `${fmtF0(presets[idx].vol)}мл D${fmtF1(presets[idx].diam)}мм`;
        } else if (idx === presets.length) {
          txt = '+ Новый';
        } else {
          txt = 'Назад';
        }
        rows.push(line(txt, c === idx));
      }
      return rows;
    }

    case 'SERVICE_SYRINGE_EDIT': {
      const pidx = ui.editingPresetIdx;
      const p    = (pidx >= 0 && pidx < st.settings.presets.length)
                   ? st.settings.presets[pidx] : {vol:0,diam:0};
      const hasDel = st.settings.presets.length > 1;
      const delI   = hasDel ? 2 : -1;
      const backI  = hasDel ? 3 : 2;
      const rows = [
        line(`  Шприц #${pidx+1}`),
        line(`Объём: ${fmtF0(p.vol)} мл`,   c===0, c===0&&ed),
        line(`D ext: ${fmtF1(p.diam)} мм`,  c===1, c===1&&ed),
        line(''),
      ];
      if (hasDel) rows.push(line('[X] Удалить', c===delI));
      rows.push(line('Назад', c===backI));
      return rows;
    }

    default: return [line('---')];
  }
}

function line(text, cursor=false, editing=false, small=false) {
  return { text: String(text), cursor, editing, small };
}
function sep() { return { text: '─'.repeat(21), cursor: false, editing: false, sep: true, small: false }; }

function renderOled(st) {
  const lines  = getLines(st);
  const el     = document.getElementById('oled-screen');
  let html     = '';
  for (const l of lines) {
    const mark = l.cursor ? '▶' : ' ';
    const raw  = l.sep ? l.text : pad(mark + l.text, 22);
    if (l.editing) {
      html += mark + `<span class="blink sel">${esc(l.text.padEnd(20))}</span>\n`;
    } else if (l.cursor && !l.sep) {
      html += `<span class="sel">${esc(raw)}</span>\n`;
    } else if (l.small) {
      html += `<span style="opacity:0.5">${esc(raw)}</span>\n`;
    } else {
      html += esc(raw) + '\n';
    }
  }
  el.innerHTML = html;

  const overlay = document.getElementById('sleep-overlay');
  overlay.classList.toggle('active', !!st.ui.displaySleeping);
}

// ─── Switches ─────────────────────────────────────────────────────────────
function renderSwitches(sw) {
  ['top','a','b','bot'].forEach(k => {
    const el = document.getElementById('sw-' + k);
    if (el) el.classList.toggle('on', !!sw[k]);
  });
}

// ─── Desktop direct-inputs ────────────────────────────────────────────────
function updateDesktopInputs(st) {
  if (mode !== 'desktop') return;
  const cy   = st.cycle;
  const sets = st.settings;
  const scr  = st.screen;

  // Syringe A select
  const selA = document.getElementById('ds-syringe-a');
  rebuildPresetSelect(selA, sets.presets, cy.syringeA.presetIdx);
  const selB = document.getElementById('ds-syringe-b');
  rebuildPresetSelect(selB, sets.presets, cy.syringeB.presetIdx);

  setInputVal('ds-diam-a',    cy.syringeA.diameter);
  setInputVal('ds-diam-b',    cy.syringeB.diameter);
  setInputVal('ds-dose-time', cy.doseTimeMin);
  setInputVal('ds-pitch',     sets.screwPitch);
  setInputVal('ds-sleep',     sets.sleepTimeout);

  // Disable irrelevant inputs/buttons
  const isParked  = scr === 'PARKED';
  const isCharged = scr === 'CHARGED';
  const isDosing  = scr === 'DOSING';
  const isDone    = scr === 'DONE';

  disableEl('ds-syringe-a', !isParked);
  disableEl('ds-diam-a',    !isParked);
  disableEl('ds-syringe-b', !isParked);
  disableEl('ds-diam-b',    !isParked);
  disableEl('ds-dose-time', !isCharged);

  // Command buttons
  setCmd('start_charging', !isParked);
  setCmd('pusk',           !isCharged);
  setCmd('abort',          !isDosing);
  setCmd('new_cycle',      !isDone);

  // Debug switch states
  document.querySelectorAll('.dsw-btn').forEach(btn => {
    const k = btn.dataset.sw;
    btn.classList.toggle('active', !!st.switches[k]);
    btn.querySelector('.led').classList.toggle('on', !!st.switches[k]);
  });
}

function rebuildPresetSelect(sel, presets, activeIdx) {
  if (!sel) return;
  const prev = sel.value;
  sel.innerHTML = '';
  presets.forEach((p, i) => {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = `${fmtF0(p.vol)} мл (D${fmtF1(p.diam)})`;
    sel.appendChild(opt);
  });
  sel.value = activeIdx >= 0 ? String(activeIdx) : '0';
}

function setInputVal(id, val) {
  const el = document.getElementById(id);
  if (el && document.activeElement !== el) el.value = parseFloat(val).toFixed(1);
}

function disableEl(id, dis) {
  const el = document.getElementById(id);
  if (el) el.disabled = dis;
}

function setCmd(cmd, dis) {
  const btn = document.querySelector(`[data-cmd="${cmd}"]`);
  if (btn) btn.disabled = dis;
}

// ─── Main render ──────────────────────────────────────────────────────────
function render(st) {
  renderOled(st);
  renderSwitches(st.switches);
  updateDesktopInputs(st);
}

// ─── Log ──────────────────────────────────────────────────────────────────
function appendLog(level, msg) {
  const panel = document.getElementById('log-panel');
  if (!panel) return;
  const d = document.createElement('div');
  d.className = level;
  d.textContent = `[${new Date().toLocaleTimeString()}] ${msg}`;
  panel.appendChild(d);
  while (panel.children.length > 200) panel.removeChild(panel.firstChild);
  panel.scrollTop = panel.scrollHeight;
}

// ─── Encoder buttons ──────────────────────────────────────────────────────
document.querySelectorAll('[data-ev]').forEach(btn => {
  btn.addEventListener('click', () => {
    send({ type: 'encoder', action: btn.dataset.ev });
  });
});

// ─── Command buttons ──────────────────────────────────────────────────────
document.querySelectorAll('[data-cmd]').forEach(btn => {
  btn.addEventListener('click', () => {
    send({ type: 'command', action: btn.dataset.cmd });
  });
});

// ─── Debug switch buttons ─────────────────────────────────────────────────
document.querySelectorAll('.dsw-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const active = btn.classList.contains('active');
    send({ type: 'debug_switch', switch: btn.dataset.sw, state: !active });
  });
});

// ─── Direct-set inputs ────────────────────────────────────────────────────
function addDirectSet(id, field) {
  const el = document.getElementById(id);
  if (!el) return;
  el.addEventListener('change', () => {
    send({ type: 'direct_set', field, value: parseFloat(el.value) });
  });
}
addDirectSet('ds-diam-a',    'syringeA.diameter');
addDirectSet('ds-diam-b',    'syringeB.diameter');
addDirectSet('ds-dose-time', 'doseTimeMin');
addDirectSet('ds-pitch',     'screwPitch');
addDirectSet('ds-sleep',     'sleepTimeout');

const selA = document.getElementById('ds-syringe-a');
if (selA) selA.addEventListener('change', () => {
  send({ type: 'direct_set', field: 'syringeA.presetIdx', value: parseInt(selA.value) });
});
const selB = document.getElementById('ds-syringe-b');
if (selB) selB.addEventListener('change', () => {
  send({ type: 'direct_set', field: 'syringeB.presetIdx', value: parseInt(selB.value) });
});

// ─── Start ────────────────────────────────────────────────────────────────
connect();
