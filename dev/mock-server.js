'use strict';
/*
 * DOZATOR — локальный мок для разработки UI без микроконтроллера.
 *
 * Поднимает HTTP (отдаёт ../data) и WebSocket /ws, который говорит на том же
 * протоколе, что и прошивка (см. WEBSOCKET_PROTOCOL.md): шлёт `state`,
 * принимает command/direct_set/set_preset/debug_switch и имитирует автомат
 * (срабатывание концевиков по таймерам, прогресс дозирования).
 *
 * Запуск:
 *   cd dev && npm install && npm start
 *   открыть http://localhost:8000
 *
 * Этот код НЕ попадает в прошивку и НЕ льётся на устройство.
 */

const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer } = require('ws');

const PORT = process.env.PORT || 8000;
const DATA_DIR = path.join(__dirname, '..', 'data');

// Ускорение времени, чтобы не ждать реальные минуты при просмотре цикла.
const TIME_FACTOR = 30;     // дозирование идёт в 30× быстрее реального
const T_PARK_TOP = 1200;    // мс до срабатывания TOP в PARKING
const T_CHARGE_A = 900;     // мс до A в CHARGING
const T_CHARGE_B = 1700;    // мс до B (A + 800)

// ── Мок-состояние (зеркало AppState) ───────────────────────────────────────
const state = {
  screen: 'IDLE',
  doneReason: 'timer',
  fullPathSteps: 120000,   // H (демо: откалибровано)
  screwPitch: 8.0,
  sleepTimeout: 15,
  parkSpeed: 800,
  chargeSpeed: 400,
  presets: [
    { vol: 10, diam: 14.5 },
    { vol: 20, diam: 19.0 },
    { vol: 50, diam: 28.6 },
  ],
  syringeA: { presetIdx: 1, diameter: 19.0 },
  syringeB: { presetIdx: 2, diameter: 28.6 },
  doseTimeMin: 2.0,
  switches: { top: false, a: false, b: false, bot: false },
  rawSwitches: { top: false, a: false, b: false, bot: false },
  dosing: { elapsedSec: 0, totalSec: 0, progress: 0, volumeA: 0, volumeB: 0 },
};

let screenEnterMs = Date.now();
let dosingStartMs = 0;

// ── Расчёты (порт src/app/Calculations.cpp) ─────────────────────────────────
const PI = 3.14159265;
const area = (d) => PI * (d / 2) * (d / 2);
const volOf = (s) => (state.presets[s.presetIdx] ? state.presets[s.presetIdx].vol : 0);
const flowA = () => (state.doseTimeMin > 0 ? volOf(state.syringeA) / state.doseTimeMin : 0);
const screwSpeed = () => { const sA = area(state.syringeA.diameter); return sA > 0 ? flowA() * 1000 / sA : 0; };
const flowB = () => screwSpeed() * area(state.syringeB.diameter) / 1000;

// Физически допустимый диапазон времени дозирования (порт calc::timeRangeMin).
function timeRange() {
  const vA = volOf(state.syringeA);
  if (vA <= 0) return { min: 0, max: 0 };
  let tMin = vA / 3000;   // MAX_FLOW_MLPM
  let tMax = vA / 0.33;   // MIN_FLOW_MLPM
  const sA = area(state.syringeA.diameter);
  if (sA > 0 && state.screwPitch > 0) {
    tMin = Math.max(tMin, vA * 1000 / (300 * sA * state.screwPitch));  // MOTOR_RPM_MAX
    tMax = Math.min(tMax, vA * 1000 / (1 * sA * state.screwPitch));    // MOTOR_RPM_MIN
  }
  if (tMin > tMax) tMin = tMax;
  return { min: tMin, max: tMax };
}

// ── Сборка state JSON (зеркало WsProtocol::buildStateJson) ──────────────────
function buildState() {
  return {
    type: 'state',
    screen: state.screen,
    settings: {
      screwPitch: state.screwPitch,
      sleepTimeout: state.sleepTimeout,
      parkSpeed: state.parkSpeed,
      chargeSpeed: state.chargeSpeed,
      fullPathSteps: state.fullPathSteps,
      presets: state.presets,
    },
    cycle: {
      syringeA: { presetIdx: state.syringeA.presetIdx, diameter: state.syringeA.diameter, volume: volOf(state.syringeA) },
      syringeB: { presetIdx: state.syringeB.presetIdx, diameter: state.syringeB.diameter, volume: volOf(state.syringeB) },
      doseTimeMin: state.doseTimeMin,
      flowA: flowA(),
      flowB: flowB(),
      timeMin: timeRange().min,
      timeMax: timeRange().max,
      planVolA: volOf(state.syringeA),   // демо-план (в прошивке — по расстоянию L2)
      planVolB: volOf(state.syringeB),
    },
    switches: state.switches,
    rawSwitches: state.rawSwitches,
    dosing: { ...state.dosing, reason: state.doneReason },
  };
}

// ── Автомат (зеркало StateMachine::transitionTo) ────────────────────────────
function transitionTo(next) {
  console.log(`SM: ${state.screen} -> ${next}`);
  state.screen = next;
  screenEnterMs = Date.now();
  switch (next) {
    case 'IDLE':
    case 'CALIBRATING':
      state.switches = { top: false, a: false, b: false, bot: false };
      break;
    case 'PARKED':
      state.switches = { top: true, a: false, b: false, bot: false };
      break;
    case 'CHARGING':
      state.switches = { top: false, a: false, b: false, bot: false };
      break;
    case 'CHARGED':
      state.switches.a = true; state.switches.b = true;
      break;
    case 'DOSING':
      state.dosing = { elapsedSec: 0, totalSec: state.doseTimeMin * 60, progress: 0, volumeA: 0, volumeB: 0 };
      dosingStartMs = Date.now();
      break;
    case 'DONE':
      state.switches.bot = (state.doneReason === 'bot');   // bot true только если реально сработал
      break;
    case 'PARKING':
      state.switches = { top: false, a: false, b: false, bot: false };
      break;
  }
  // rawSwitches в моке = текущее сценарное состояние (чтобы панель мигала по циклу)
  state.rawSwitches = { ...state.switches };
  broadcast();
}

// ── Тик имитации (концевики по таймерам + прогресс дозирования) ─────────────
function tick() {
  const now = Date.now();
  const elapsed = now - screenEnterMs;
  let changed = false;

  switch (state.screen) {
    case 'CALIBRATING':
      if (elapsed >= 2500) { state.fullPathSteps = 120000; transitionTo('IDLE'); return; }
      break;

    case 'PARKING':
      if (elapsed >= T_PARK_TOP) { state.rawSwitches.top = true; transitionTo('PARKED'); return; }
      break;
    case 'CHARGING':
      if (elapsed >= T_CHARGE_A && !state.rawSwitches.a) { state.rawSwitches.a = state.switches.a = true; changed = true; }
      if (elapsed >= T_CHARGE_B && !state.rawSwitches.b) { state.rawSwitches.b = state.switches.b = true; changed = true; }
      if (state.switches.a && state.switches.b) { transitionTo('CHARGED'); return; }
      break;
    case 'DOSING': {
      const simElapsed = Math.min(((now - dosingStartMs) / 1000) * TIME_FACTOR, state.dosing.totalSec);
      state.dosing.elapsedSec = simElapsed;
      state.dosing.progress = state.dosing.totalSec > 0 ? simElapsed / state.dosing.totalSec : 0;
      state.dosing.volumeA = volOf(state.syringeA) * state.dosing.progress;
      state.dosing.volumeB = volOf(state.syringeB) * state.dosing.progress;
      changed = true;
      // ДЕМО: имитируем срабатывание концевика BOT раньше таймера (на 85%),
      // чтобы показать план/факт. progress НЕ форсим в 1 — фиксируем факт.
      if (state.dosing.progress >= 0.85) { state.doneReason = 'bot'; transitionTo('DONE'); return; }
      if (simElapsed >= state.dosing.totalSec) { state.dosing.progress = 1; state.doneReason = 'timer'; transitionTo('DONE'); return; }
      break;
    }
  }
  if (changed) broadcast();
}

// ── Обработка входящих сообщений (зеркало WsProtocol) ───────────────────────
function handleMessage(msg) {
  switch (msg.type) {
    case 'command': return handleCommand(msg.action);
    case 'direct_set': return handleDirectSet(msg.field, msg.value);
    case 'set_preset': return handleSetPreset(msg.index, msg.vol, msg.diam);
    case 'debug_switch': return handleDebugSwitch(msg.switch, !!msg.state);
  }
}

function handleCommand(action) {
  if (action === 'reset') return transitionTo('PARKING');   // стоп + парковка из любого состояния
  if (action === 'calibrate' && ['IDLE', 'DONE', 'PARKED'].includes(state.screen)) return transitionTo('CALIBRATING');
  if (action === 'park' && (state.screen === 'IDLE' || state.screen === 'DONE')) transitionTo('PARKING');
  else if (action === 'start_charging' && state.screen === 'PARKED') transitionTo('CHARGING');
  else if (action === 'pusk' && state.screen === 'CHARGED' && state.fullPathSteps > 0) transitionTo('DOSING');
  else if (action === 'abort' && state.screen === 'DOSING') { state.doneReason = 'abort'; transitionTo('DONE'); }
  else if (action === 'new_cycle' && state.screen === 'DONE') transitionTo('IDLE');
}

function handleDirectSet(field, value) {
  const v = Number(value);
  const inParked = state.screen === 'PARKED';
  if (field === 'doseTimeMin' && state.screen === 'CHARGED') state.doseTimeMin = clamp(v, 0.3, 20);
  else if (field === 'syringeA.presetIdx' && inParked) { state.syringeA.presetIdx = v | 0; state.syringeA.diameter = state.presets[v | 0]?.diam ?? state.syringeA.diameter; }
  else if (field === 'syringeA.diameter' && inParked) state.syringeA.diameter = clamp(v, 1, 150);
  else if (field === 'syringeB.presetIdx' && inParked) { state.syringeB.presetIdx = v | 0; state.syringeB.diameter = state.presets[v | 0]?.diam ?? state.syringeB.diameter; }
  else if (field === 'syringeB.diameter' && inParked) state.syringeB.diameter = clamp(v, 1, 150);
  else if (field === 'screwPitch') state.screwPitch = clamp(v, 0.5, 20);
  else if (field === 'sleepTimeout') state.sleepTimeout = clamp(v, 5, 300) | 0;
  else if (field === 'parkSpeed') state.parkSpeed = clamp(v, 50, 15000);
  else if (field === 'chargeSpeed') state.chargeSpeed = clamp(v, 50, 15000);
  broadcast();
}

function handleSetPreset(index, vol, diam) {
  if (index < 0 || index >= state.presets.length) return;
  state.presets[index] = { vol: clamp(Number(vol), 1, 500), diam: clamp(Number(diam), 1, 150) };
  if (state.syringeA.presetIdx === index) state.syringeA.diameter = state.presets[index].diam;
  if (state.syringeB.presetIdx === index) state.syringeB.diameter = state.presets[index].diam;
  broadcast();
}

function handleDebugSwitch(sw, on) {
  if (['top', 'a', 'b', 'bot'].includes(sw)) { state.switches[sw] = on; state.rawSwitches[sw] = on; broadcast(); }
}

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

// ── HTTP (статика) + WebSocket ──────────────────────────────────────────────
const MIME = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' };

const server = http.createServer((req, res) => {
  let urlPath = decodeURIComponent(req.url.split('?')[0]);
  if (urlPath === '/' || urlPath === '') urlPath = '/index.html';
  const file = path.join(DATA_DIR, path.normalize(urlPath).replace(/^(\.\.[/\\])+/, ''));
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end('404'); return; }
    res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
    res.end(data);
  });
});

const wss = new WebSocketServer({ server, path: '/ws' });
function broadcast() {
  const json = JSON.stringify(buildState());
  wss.clients.forEach((c) => { if (c.readyState === 1) c.send(json); });
}
wss.on('connection', (ws) => {
  console.log('WS client connected');
  ws.send(JSON.stringify(buildState()));
  ws.on('message', (raw) => { try { handleMessage(JSON.parse(raw.toString())); } catch {} });
});

setInterval(tick, 100);

server.listen(PORT, () => {
  console.log(`DOZATOR mock → http://localhost:${PORT}  (Ctrl+C to stop)`);
});
