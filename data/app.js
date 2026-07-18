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
  'doseTimeMin':       { min: 0.3, max: 20  },
  'screwPitch':        { min: 0.5, max: 20  },
  'parkSpeed':         { min: 50,  max: 15000 },
  'chargeSpeed':       { min: 50,  max: 15000 },
  'fullPathSteps':     { min: 0,   max: 5000000 },
  'jogSteps':          { min: 1,   max: 100000 },
  'jogSpeed':          { min: 50,  max: 15000 },
};
const DOSE_TIME_MIN = 0.3;   // жёсткий пол времени дозирования, мин
const DOSE_TIME_MAX = 20;    // жёсткий потолок, мин
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
    return {
      min: Math.max(lastState.cycle.timeMin, DOSE_TIME_MIN),
      max: Math.min(lastState.cycle.timeMax, DOSE_TIME_MAX),
    };
  }
  return RANGES[field];
}
const fmtRange = (r) => `${fmt(Math.ceil(r.min * 100) / 100)}–${fmt(Math.floor(r.max * 100) / 100)}`;

const STAGE_RU = {
  IDLE:        'Простой',
  CALIBRATING: 'Калибровка',
  STOPPED:     'Остановлено',
  DEBUG:       'Отладка',
  PARKING:  'Парковка',
  PARKED:   'Готов',
  CHARGING: 'Зарядка',
  CHARGED:  'Заряжено',
  DOSING:   'Дозирование',
  DONE:     'Готово',
};
const KNOWN = ['IDLE', 'CALIBRATING', 'STOPPED', 'DEBUG', 'PARKING', 'PARKED', 'CHARGING', 'CHARGED', 'DOSING', 'DONE'];

// ─── Render ─────────────────────────────────────────────────────────────────
function render(s) {
  // 1) Всегда: концевики (живые показания для отладки)
  renderSwitches(s.rawSwitches || s.switches || {});

  // 2) Стадия
  const screen = s.screen || '';
  $('stage-tag').textContent = STAGE_RU[screen] || screen || '—';

  // Двухфазная кнопка сброса: обычно «СТОП»; после остановки — «Парковка».
  const rb = $('btn-reset');
  if (rb) {
    if (screen === 'STOPPED') { rb.textContent = '⌂ Парковка'; rb.dataset.cmd = 'park'; }
    else { rb.textContent = '■ СТОП'; rb.dataset.cmd = 'stop'; }
  }

  const target = KNOWN.includes(screen) ? screen : 'OTHER';
  document.querySelectorAll('.screen').forEach((el) => {
    el.hidden = (el.dataset.screen !== target);
  });
  if (target === 'OTHER') $('other-title').textContent = screen || '…';

  // 3) Содержимое активного экрана
  switch (screen) {
    case 'IDLE':        renderIdle(s);        break;
    case 'CALIBRATING': renderCalibrating(s); break;
    case 'STOPPED':     renderStopped(s);     break;
    case 'DEBUG':       renderDebug(s);       break;
    case 'PARKED':      renderParked(s);      break;
    case 'CHARGED':     renderCharged(s);     break;
    case 'DOSING':      renderDosing(s);      break;
    case 'DONE':        renderDone(s);        break;
  }
  if (screen !== 'DOSING') stopwatchStop();

  // 4) Настройки и сервис (в любой момент)
  renderSettings(s);
  renderService(s);
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

function renderIdle(s) {
  const H = s.settings?.fullPathSteps || 0;
  const el = $('calib-status');
  if (el) el.textContent = H > 0
    ? `Калибровка: OK (полный ход H = ${H} шаг)`
    : '⚠ Не откалибровано — открой «🔧 Калибровка / сервис» ниже';
}

function renderCalibrating(s) {
  const phase = s.ui?.calibPhase || 0;
  const el = $('calib-phase');
  if (el) el.textContent = phase === 0
    ? 'Ищу дом — концевик TOP…'
    : 'Измеряю полный ход до конца — концевик BOT…';
}

function renderService(s) {
  const H = s.settings?.fullPathSteps || 0;
  setVal($('set-H'), H);
  const st = $('service-status');
  if (st) st.textContent = H > 0
    ? `✓ Откалибровано: H = ${H} шаг (≈ ${fmtCm(H)} см).`
    : '⚠ Не откалибровано. Нажми «Калибровать ход» (сними шприцы).';
}

function renderDebug(s) {
  const pos = s.ui?.motorPos ?? 0;
  if ($('jog-pos')) $('jog-pos').textContent = pos;
  if ($('jog-pos-cm')) $('jog-pos-cm').textContent = fmtCm(pos);
  setVal($('set-jogsteps'), s.settings?.jogSteps ?? '');
  setVal($('set-jogspeed'), s.settings?.jogSpeed ?? '');
}

function renderStopped(s) {
  const cause = s.ui?.stopCause || 'manual';
  const map = {
    manual:       'Мотор остановлен. Нажми «Парковка» (кнопка вверху) для возврата домой.',
    stuck_home:   '⚠ Сбой: не найден концевик TOP (дом). Проверь концевик и направление, затем «Парковка».',
    stuck_charge: '⚠ Сбой: концевики A/B не прижались при зарядке. Проверь механику, затем «Парковка».',
    stuck_end:    '⚠ Сбой: не найден концевик BOT (конец) при калибровке. Проверь концевик, затем «Парковка».',
    calib_fail:   '⚠ Калибровка не удалась (ход слишком мал). Проверь концевики и повтори калибровку.',
  };
  const el = $('stopped-msg');
  if (el) el.textContent = map[cause] || map.manual;
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
    ? `Время дозирования, мин (${fmtRange({ min: Math.max(c.timeMin, DOSE_TIME_MIN), max: Math.min(c.timeMax, DOSE_TIME_MAX) })})`
    : 'Время дозирования, мин';

  // ПУСК только если откалибровано (задан полный ход H).
  const H = s.settings?.fullPathSteps || 0;
  const pusk = document.querySelector('[data-cmd="pusk"]');
  if (pusk) {
    pusk.disabled = (H <= 0);
    pusk.textContent = (H > 0) ? '▶ ПУСК' : '▶ Нужна калибровка';
  }
}

// ─── Секундомер дозирования (тикает на клиенте, синхронно с elapsedSec) ──────
let swTimer = null, swBaseSec = 0, swBaseAt = 0;
function fmtMMSS(sec) {
  sec = Math.max(0, Math.floor(sec));
  return `${Math.floor(sec / 60)}:${String(sec % 60).padStart(2, '0')}`;
}
function stopwatchSync(elapsedSec) {
  swBaseSec = elapsedSec || 0;
  swBaseAt = Date.now();
  const el = $('dose-sw');
  if (el) el.textContent = fmtMMSS(swBaseSec);
  if (!swTimer) {
    swTimer = setInterval(() => {
      const el2 = $('dose-sw');
      if (el2) el2.textContent = fmtMMSS(swBaseSec + (Date.now() - swBaseAt) / 1000);
    }, 250);
  }
}
function stopwatchStop() { if (swTimer) { clearInterval(swTimer); swTimer = null; } }

function renderDosing(s) {
  const d = s.dosing || {};
  const pct = Math.round((d.progress || 0) * 100);
  $('dose-pct').textContent = pct + '%';
  $('dose-fill').style.width = pct + '%';
  $('dose-elapsed').textContent = Math.round(d.elapsedSec || 0);
  $('dose-total').textContent = Math.round(d.totalSec || 0);
  $('vol-a').textContent = fmt(d.volumeA);
  $('vol-b').textContent = fmt(d.volumeB);
  stopwatchSync(d.elapsedSec);
}

function renderDone(s) {
  const c = s.cycle || {}, d = s.dosing || {};
  const planA = c.planVolA || 0;
  const planB = c.planVolB || 0;
  $('pf-plan-a').textContent = fmt(planA);
  $('pf-fact-a').textContent = fmt(d.volumeA || 0);
  $('pf-plan-b').textContent = fmt(planB);
  $('pf-fact-b').textContent = fmt(d.volumeB || 0);
  $('pf-plan-t').textContent = Math.round(d.totalSec || 0);
  $('pf-fact-t').textContent = Math.round(d.elapsedSec || 0);
  const pct = Math.round((d.progress || 0) * 100);
  $('pf-pct').textContent = pct + '%';
  const reason = d.reason || 'timer';
  const note = $('pf-note');
  if (reason === 'bot') {
    note.textContent = '⚠ Концевик BOT сработал раньше — выдавлено меньше плана';
    note.classList.add('warn');
  } else if (reason === 'abort') {
    note.textContent = '■ Остановлено вручную (СТОП) — выдавлено меньше плана';
    note.classList.add('warn');
  } else {
    note.textContent = '✓ Выдавлен полный плановый объём';
    note.classList.remove('warn');
  }
}

function renderSettings(s) {
  setVal($('set-pitch'), fmt(s.settings?.screwPitch));
  setVal($('set-parkspeed'), fmt(s.settings?.parkSpeed));
  setVal($('set-chargespeed'), fmt(s.settings?.chargeSpeed));
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
// ─── Пересчёт шагов → см (микрошаг 1/16 → 3200 шаг/об; ход винта из настроек) ─
const STEPS_PER_REV = 3200;   // 200 × 16 (совпадает с FULL_STEPS_PER_REV в прошивке)
function stepsToMm(steps) {
  const pitch = lastState?.settings?.screwPitch || 8;   // мм/об
  return steps * pitch / STEPS_PER_REV;
}
function fmtCm(steps) { return (Math.round(stepsToMm(steps) / 10 * 100) / 100).toFixed(2); }

// ─── Справка (модалка) ──────────────────────────────────────────────────────
const HELP = {
  overview: `<b>DOZATOR — двойной шприцевой дозатор</b><br><br>
    Один мотор двигает каретку с двумя шприцами. Рабочий цикл:<br>
    <b>Парковка</b> (домой) → <b>Зарядка</b> (опустить до контакта штоков) → задать <b>время</b> →
    <b>ПУСК</b> (выдавить за это время) → <b>Готово</b> (план/факт).<br><br>
    Перед первым использованием — <b>калибровка хода</b> (в разделе «🔧 Калибровка / сервис»).<br><br>
    Вверху всегда видна <b>панель концевиков</b> (TOP/A/B/BOT) — живое состояние для отладки:
    <b>TOP</b> = дом, <b>A/B</b> = заряд, <b>BOT</b> = конец. Кнопка <b>«■ СТОП»</b> вверху — аварийный
    стоп (повторное нажатие — парковка).`,
  calibration: `<b>Калибровка хода — как работает</b><br><br>
    Цель: измерить полный ход каретки <b>в шагах мотора</b>, чтобы дозирование было точным
    независимо от шага винта и микрошага.<br><br>
    <b>Что происходит:</b><br>
    1. Каретка едет <b>вверх до концевика TOP</b> (точка «дом»). Позиция обнуляется в 0.<br>
    2. Затем едет <b>вниз до концевика BOT</b> (конец хода), считая шаги. Концевики A/B при этом игнорируются.<br>
    3. Число шагов от TOP до BOT = <b>H</b> (полный ход). Сохраняется в память.<br><br>
    <b>Бизнес-логика дозы:</b> при зарядке каретка опускается до прижатия A и B — это позиция
    заряда <b>L</b> (зависит от длины штоков/загрузки). Остаток до конца — <b>L2 = H − L</b>.
    Это и есть рабочий ход дозы. Задаёшь время t → скорость <b>V = L2 / t</b>, едем от заряда до конца за это время.<br><br>
    <b>Условия:</b> рабочие концевики <b>TOP и BOT</b>, шприцы сняты (идёт весь ход). Если за ~2 мин
    концевик не найден — стоп с ошибкой. H хранится до следующей калибровки.`,
  charge: `<b>Зарядка</b><br><br>
    Каретка плавно опускается, пока не прижмутся <b>оба</b> концевика A и B (штоки легли на плунжеры).
    Эта позиция = «заряд» (L). Скорость — настройка «Скорость зарядки». Доступна только после калибровки.`,
  doseTimeMin: `<b>Время дозирования, мин</b><br><br>
    За сколько минут выдавить остаток хода (заряд→конец, L2). Влияет только на скорость мотора: V = L2 / время.<br>
    Диапазон под полем зависит от объёма/диаметра/шага (пределы потока и оборотов мотора).`,
  screwPitch: `<b>Шаг винта (ход/lead), мм/об</b><br><br>
    На сколько мм каретка проходит за <b>один оборот мотора</b>. Не путать с шагом резьбы!<br>
    Типовой Tr8×8 (частый на 3D-принтерах) = <b>8 мм/об</b>; Tr8×2 = 2 мм/об.<br><br>
    <b>Влияние:</b> тайминг движения от него <b>не зависит</b> (ход меряется в шагах). Нужен только для
    пересчёта шагов в <b>см и мл</b> для отображения. Если см/объём врут — поправь это значение.`,
  parkSpeed: `<b>Скорость парковки, шаг/с</b><br><br>
    Скорость движения к дому (TOP) при парковке и в фазе поиска дома при калибровке. 50–15000.`,
  chargeSpeed: `<b>Скорость зарядки, шаг/с</b><br><br>
    Скорость плавного спуска к концевикам A/B. Обычно ниже парковочной, чтобы аккуратно лечь на штоки. 50–15000.`,
  fullPathSteps: `<b>Полный ход H, шагов</b><br><br>
    Результат калибровки: шаги мотора от дома (TOP) до конца (BOT). Обычно ставится калибровкой автоматически.
    Правь вручную только если знаешь точное значение. <b>0 = не откалибровано</b> (зарядка и ПУСК заблокированы).`,
  jog: `<b>Отладка (jog)</b><br><br>
    Ручное движение мотора для проверки механики, <b>игнорируя концевики</b>.<br>
    «Шагов за нажатие» — насколько сдвинется каретка по ▲/▼. «Скорость» — как быстро.<br>
    ⚠️ Концевики не остановят — следи за кареткой. Настройки сохраняются.`,
  syringe: `<b>Шприц A / B</b><br><br>
    Объём (пресет) и диаметр шприца. Диаметр <b>не влияет на движение</b> (оно по расстоянию), нужен только
    для оценки <b>объёма в мл</b> (площадь × ход). Оба шприца двигаются вместе одной кареткой.`,
  switches: `<b>Концевики (панель вверху)</b><br><br>
    Живое состояние 4 концевиков, видно на любом экране:<br>
    <b>TOP</b> — дом (верх, ноль). <b>A / B</b> — заряд (штоки прижаты). <b>BOT</b> — конец хода (низ).<br>
    Загорается зелёным при замыкании. Удобно проверять пайку/полярность.`,
  position: `<b>Позиция мотора</b><br><br>
    Положение каретки в <b>шагах</b> от дома. Это <b>микрошаги</b>: при 1/16 один оборот = 3200 шагов,
    поэтому числа крупные — это норма. Рядом эквивалент в <b>см</b> (через «Шаг винта») — так нагляднее.`,
};

let modalAction = null;
function showModal(html, action) {
  $('modal-msg').innerHTML = html;
  const okBtn = $('modal-ok');
  if (action) { okBtn.hidden = false; okBtn.textContent = action.label; modalAction = action.fn; }
  else { okBtn.hidden = true; modalAction = null; }
  $('modal').hidden = false;
}
function hideModal() { $('modal').hidden = true; modalAction = null; }
function openService() {
  hideModal();
  const svc = $('service');
  if (svc) { svc.open = true; svc.scrollIntoView({ behavior: 'smooth', block: 'center' }); }
}
$('modal-cancel').addEventListener('click', hideModal);
$('modal-ok').addEventListener('click', () => { if (modalAction) modalAction(); else hideModal(); });
$('modal').addEventListener('click', (e) => { if (e.target === $('modal')) hideModal(); });

document.addEventListener('click', (e) => {
  // Кнопки справки «?»
  const help = e.target.closest('[data-help]');
  if (help) { e.preventDefault(); showModal(HELP[help.dataset.help] || 'Нет справки', null); return; }

  const btn = e.target.closest('[data-cmd]');
  if (!btn || btn.disabled) return;
  const cmd = btn.dataset.cmd;
  const calibrated = (lastState?.settings?.fullPathSteps || 0) > 0;

  // Зарядку/дозу нельзя без калибровки — модалка с подсказкой и переходом в Сервис.
  if ((cmd === 'start_charging' || cmd === 'pusk') && !calibrated) {
    showModal('Сначала нужна <b>калибровка хода</b>.<br><br>' +
              'Открой «🔧 Калибровка / сервис» и нажми «Калибровать ход» (сними шприцы).',
              { label: 'Открыть Сервис', fn: openService });
    return;
  }
  if (cmd === 'calibrate' &&
      !confirm('Калибровка прогонит каретку на весь ход (дом → конец).\n' +
               'Снимите шприцы и убедитесь, что ход свободен.\n\nПродолжить?')) return;
  send({ type: 'command', action: cmd });
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
bindNum('set-parkspeed', 'parkSpeed');
bindNum('set-chargespeed', 'chargeSpeed');
bindNum('set-H', 'fullPathSteps');
bindNum('set-jogsteps', 'jogSteps');
bindNum('set-jogspeed', 'jogSpeed');

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
