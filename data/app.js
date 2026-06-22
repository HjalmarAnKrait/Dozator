'use strict';

// ─── WebSocket + reconnect ──────────────────────────────────────────────────
const WS_URL = `ws://${location.hostname}/ws`;
let ws = null;
let lastState = null;
let connAttempts = 0;
const BACKOFF = [500, 1000, 2000, 4000, 8000];

function connect() {
  setConn('warn', 'переподключение…');
  ws = new WebSocket(WS_URL);
  ws.onopen    = () => { connAttempts = 0; setConn('ok', 'связь есть'); };
  ws.onclose   = () => { setConn('err', 'нет связи'); scheduleReconnect(); };
  ws.onerror   = () => { try { ws.close(); } catch {} };
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

function setConn(cls, text) {
  document.getElementById('conn-dot').className = 'conn-dot ' + cls;
  document.getElementById('conn-text').textContent = text;
}

function handleMsg(msg) {
  if (msg.type === 'state') { lastState = msg; render(msg); }
}

// ─── Helpers ────────────────────────────────────────────────────────────────
const $ = (id) => document.getElementById(id);

// Не перетираем поле, пока пользователь его редактирует (фокус).
function setVal(el, v) {
  if (el && document.activeElement !== el) el.value = v;
}

const STAGE_RU = {
  PARKING:  'Парковка',
  PARKED:   'Готов',
  CHARGING: 'Зарядка',
  CHARGED:  'Заряжено',
  DOSING:   'Дозирование',
  DONE:     'Готово',
};
const KNOWN = ['PARKING', 'PARKED', 'CHARGING', 'CHARGED', 'DOSING', 'DONE'];

// ─── Render ─────────────────────────────────────────────────────────────────
function render(s) {
  // 1) Всегда: концевики (живые показания для отладки)
  renderSwitches(s.rawSwitches || s.switches || {});

  // 2) Стадия
  const screen = s.screen || '';
  $('stage-tag').textContent = STAGE_RU[screen] || screen || '—';

  const target = KNOWN.includes(screen) ? screen : 'OTHER';
  document.querySelectorAll('.screen').forEach((el) => {
    el.hidden = (el.dataset.screen !== target);
  });
  if (target === 'OTHER') $('other-title').textContent = screen || '…';

  // 3) Содержимое активного экрана
  switch (screen) {
    case 'PARKED':  renderParked(s);  break;
    case 'CHARGED': renderCharged(s); break;
    case 'DOSING':  renderDosing(s);  break;
  }

  // 4) Настройки (в любой момент)
  renderSettings(s);
}

function renderSwitches(sw) {
  for (const k of ['top', 'a', 'b', 'bot']) {
    const led = $('led-' + k);
    if (led) led.classList.toggle('on', !!sw[k]);
  }
}

function fillPresetSelect(sel, presets, chosenIdx) {
  // Перестраиваем только если изменился набор опций (иначе теряется выбор)
  const sig = presets.map((p) => `${p.vol}|${p.diam}`).join(',');
  if (sel.dataset.sig !== sig) {
    sel.innerHTML = '';
    presets.forEach((p, i) => {
      const o = document.createElement('option');
      o.value = i;
      o.textContent = `${p.vol} мл (D ${p.diam})`;
      sel.appendChild(o);
    });
    sel.dataset.sig = sig;
  }
  if (document.activeElement !== sel) sel.value = chosenIdx;
}

function renderParked(s) {
  const presets = s.settings?.presets || [];
  const a = s.cycle?.syringeA || {};
  const b = s.cycle?.syringeB || {};
  fillPresetSelect($('sel-a'), presets, a.presetIdx ?? 0);
  fillPresetSelect($('sel-b'), presets, b.presetIdx ?? 0);
  setVal($('diam-a'), fmt(a.diameter));
  setVal($('diam-b'), fmt(b.diameter));
}

function renderCharged(s) {
  setVal($('dose-time'), fmt(s.cycle?.doseTimeMin));
  $('flow-a').textContent = fmt(s.cycle?.flowA);
  $('flow-b').textContent = fmt(s.cycle?.flowB);
}

function renderDosing(s) {
  const d = s.dosing || {};
  const pct = Math.round((d.progress || 0) * 100);
  $('dose-pct').textContent = pct + '%';
  $('dose-fill').style.width = pct + '%';
  $('dose-elapsed').textContent = Math.round(d.elapsedSec || 0);
  $('dose-total').textContent = Math.round(d.totalSec || 0);
  $('vol-a').textContent = fmt(d.volumeA);
  $('vol-b').textContent = fmt(d.volumeB);
}

function renderSettings(s) {
  setVal($('set-pitch'), fmt(s.settings?.screwPitch));
  setVal($('set-sleep'), s.settings?.sleepTimeout ?? '');
  renderPresets(s.settings?.presets || []);
}

function renderPresets(presets) {
  const list = $('presets-list');
  const sig = presets.map((p) => `${p.vol}|${p.diam}`).join(',');
  if (list.dataset.sig === sig && list.childElementCount) return;
  list.dataset.sig = sig;
  list.innerHTML = '';
  presets.forEach((p, i) => {
    const row = document.createElement('div');
    row.className = 'preset-row';
    row.innerHTML =
      `<span class="preset-idx">#${i + 1}</span>` +
      `<label>vol</label><input type="number" step="1" min="1" max="500" inputmode="numeric" value="${fmt(p.vol)}" data-preset="${i}" data-field="vol">` +
      `<label>D</label><input type="number" step="0.1" min="1" max="150" inputmode="decimal" value="${fmt(p.diam)}" data-preset="${i}" data-field="diam">`;
    list.appendChild(row);
  });
}

function fmt(v) {
  if (v == null || isNaN(v)) return '';
  return (Math.round(v * 100) / 100).toString();
}

// ─── Events ─────────────────────────────────────────────────────────────────
// Команды (кнопки)
document.addEventListener('click', (e) => {
  const btn = e.target.closest('[data-cmd]');
  if (btn) send({ type: 'command', action: btn.dataset.cmd });
});

// direct_set по change (срабатывает на blur/Enter — без петель ввода)
$('sel-a').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeA.presetIdx', value: +e.target.value }));
$('sel-b').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeB.presetIdx', value: +e.target.value }));
$('diam-a').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeA.diameter', value: +e.target.value }));
$('diam-b').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeB.diameter', value: +e.target.value }));
$('dose-time').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'doseTimeMin', value: +e.target.value }));
$('set-pitch').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'screwPitch', value: +e.target.value }));
$('set-sleep').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'sleepTimeout', value: +e.target.value }));

// Редактирование пресетов (set_preset реализуется в прошивке этой же ветки)
$('presets-list').addEventListener('change', (e) => {
  const el = e.target;
  if (el.dataset.preset == null) return;
  const idx = +el.dataset.preset;
  const row = el.closest('.preset-row');
  const vol = +row.querySelector('[data-field="vol"]').value;
  const diam = +row.querySelector('[data-field="diam"]').value;
  send({ type: 'set_preset', index: idx, vol, diam });
});

connect();
