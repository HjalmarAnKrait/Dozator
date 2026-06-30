'use strict';

// ─── WebSocket + reconnect ──────────────────────────────────────────────────
// location.host включает порт (localhost:8000 локально; 192.168.4.1 на плате — порт 80).
const WS_URL = `ws://${location.host}/ws`;
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
// При установке валидного значения с сервера снимаем пометку ошибки.
function setVal(el, v) {
  if (el && document.activeElement !== el) { el.value = v; el.classList.remove('invalid'); }
}

// ─── Допустимые диапазоны (синхронны с constrain() в прошивке) ──────────────
const RANGES = {
  'syringeA.diameter': { min: 1,   max: 150 },
  'syringeB.diameter': { min: 1,   max: 150 },
  'doseTimeMin':       { min: 0.1, max: 999 },
  'screwPitch':        { min: 0.5, max: 10  },
  'sleepTimeout':      { min: 5,   max: 300 },
};
const PRESET_RANGE = { vol: { min: 1, max: 500 }, diam: { min: 1, max: 150 } };

// Парсит и проверяет диапазон. Возвращает число или null (пусто/неполно/вне диапазона).
function validNum(raw, range) {
  if (raw === '' || raw === '-' || raw === '.') return null;
  const v = parseFloat(raw);
  if (!isFinite(v) || v < range.min || v > range.max) return null;
  return v;
}
const clampNum = (v, r) => Math.min(r.max, Math.max(r.min, v));
const markInvalid = (el, bad) => el.classList.toggle('invalid', bad);

// Диапазон поля. Для времени дозирования — динамический (физический предел
// под текущие шприцы/шаг приходит в state.cycle.timeMin/timeMax).
function getRange(field) {
  if (field === 'doseTimeMin' && lastState?.cycle && lastState.cycle.timeMax > 0) {
    return { min: lastState.cycle.timeMin, max: lastState.cycle.timeMax };
  }
  return RANGES[field];
}
const fmtRange = (r) => `${fmt(Math.ceil(r.min * 100) / 100)}–${fmt(Math.floor(r.max * 100) / 100)}`;

const STAGE_RU = {
  IDLE:     'Простой',
  PARKING:  'Парковка',
  PARKED:   'Готов',
  CHARGING: 'Зарядка',
  CHARGED:  'Заряжено',
  DOSING:   'Дозирование',
  DONE:     'Готово',
};
const KNOWN = ['IDLE', 'PARKING', 'PARKED', 'CHARGING', 'CHARGED', 'DOSING', 'DONE'];

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
  const c = s.cycle || {};
  setVal($('dose-time'), fmt(c.doseTimeMin));
  $('flow-a').textContent = fmt(c.flowA);
  $('flow-b').textContent = fmt(c.flowB);
  // Подпись с физически допустимым диапазоном под текущие шприцы/шаг.
  $('dose-time-label').textContent = (c.timeMax > 0)
    ? `Время дозирования, мин (${fmtRange({ min: c.timeMin, max: c.timeMax })})`
    : 'Время дозирования, мин';
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

// Числовое поле с валидацией: на input — пересчёт сразу (если валидно),
// невалидное подсвечиваем и не шлём; на blur — поджимаем в диапазон.
function bindNum(id, field) {
  const el = $(id);
  el.addEventListener('input', () => {
    const v = validNum(el.value, getRange(field));
    markInvalid(el, v === null && el.value !== '');
    if (v !== null) send({ type: 'direct_set', field, value: v });
  });
  el.addEventListener('blur', () => {
    if (el.value === '') return;
    const n = parseFloat(el.value);
    if (!isFinite(n)) { el.value = ''; markInvalid(el, false); return; }
    const v = clampNum(n, getRange(field));
    el.value = fmt(v);
    markInvalid(el, false);
    send({ type: 'direct_set', field, value: v });
  });
}

// Селекты — по change (частичного ввода нет).
$('sel-a').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeA.presetIdx', value: +e.target.value }));
$('sel-b').addEventListener('change', (e) =>
  send({ type: 'direct_set', field: 'syringeB.presetIdx', value: +e.target.value }));

bindNum('diam-a',    'syringeA.diameter');
bindNum('diam-b',    'syringeB.diameter');
bindNum('dose-time', 'doseTimeMin');
bindNum('set-pitch', 'screwPitch');
bindNum('set-sleep', 'sleepTimeout');

// Редактирование пресетов — на лету, с валидацией каждого поля.
$('presets-list').addEventListener('input', (e) => {
  const el = e.target;
  if (el.dataset.preset == null) return;
  const range = PRESET_RANGE[el.dataset.field];
  markInvalid(el, validNum(el.value, range) === null && el.value !== '');
  const row = el.closest('.preset-row');
  const vol  = validNum(row.querySelector('[data-field="vol"]').value,  PRESET_RANGE.vol);
  const diam = validNum(row.querySelector('[data-field="diam"]').value, PRESET_RANGE.diam);
  if (vol === null || diam === null) return;  // шлём только когда оба валидны
  send({ type: 'set_preset', index: +el.dataset.preset, vol, diam });
});

connect();
