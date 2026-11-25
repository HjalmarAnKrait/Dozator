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
  calibPhase: 0,
  fullPathSteps: 120000,   // H (демо: откалибровано)
  jogSteps: 200,
  jogSpeed: 800,
  motorPos: 0,
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
  planVolA: 0,
  planVolB: 0,
};

// Демо сбоя: MOCK_BOT_AT=0.85 — BOT срабатывает на 85 % хода дозы (план/факт).
const BOT_AT = Number(process.env.MOCK_BOT_AT) || 0;
let dosingStartPos = 0;

let screenEnterMs = Date.now();
let dosingStartMs = 0;

// ── Расчёты (порт src/app/Calculations.cpp) ─────────────────────────────────
// Константы — из include/config.h.
const PI = 3.14159265;
const FULL_STEPS_PER_REV = 200 * 16;
const MAX_FLOW_MLPM = 3000;
const MOTOR_SPS_MAX = 6400;
const DOSE_SPS_MIN = 1;
const DOSE_TIME_MAX_MIN = 20;
const CHARGE_AT = 0.3;      // демо: заряд (2) — на 30 % хода H

const area = (d) => PI * (d / 2) * (d / 2);
const volOf = (s) => (state.presets[s.presetIdx] ? state.presets[s.presetIdx].vol : 0);
const stepsToMm = (steps) => steps * state.screwPitch / FULL_STEPS_PER_REV;
const distToVol = (diam, mm) => area(diam) * mm / 1000;

// Объём дозы: в CHARGED — по оставшемуся ходу H − L, в DOSING/DONE — план, иначе пресет.
function doseVol(s, plan) {
  if (state.screen === 'CHARGED' && state.fullPathSteps > state.motorPos)
    return distToVol(s.diameter, stepsToMm(state.fullPathSteps - state.motorPos));
  if ((state.screen === 'DOSING' || state.screen === 'DONE') && plan > 0) return plan;
  return volOf(s);
}
const doseVolA = () => doseVol(state.syringeA, state.planVolA);
const doseVolB = () => doseVol(state.syringeB, state.planVolB);
const flowA = () => (state.doseTimeMin > 0 ? doseVolA() / state.doseTimeMin : 0);
const screwSpeed = () => { const sA = area(state.syringeA.diameter); return sA > 0 ? flowA() * 1000 / sA : 0; };
const flowB = () => screwSpeed() * area(state.syringeB.diameter) / 1000;

// Физически допустимый диапазон времени дозирования (порт calc::timeRangeMin).
function timeRange() {
  const vA = doseVolA();
  if (vA <= 0) return { min: 0, max: 0 };
  let tMin = vA / MAX_FLOW_MLPM;
  let tMax = DOSE_TIME_MAX_MIN;
  const sA = area(state.syringeA.diameter);
  if (sA > 0 && state.screwPitch > 0) {
    const steps = vA * 1000 / sA / state.screwPitch * FULL_STEPS_PER_REV;
    tMin = Math.max(tMin, steps / MOTOR_SPS_MAX / 60);
    tMax = Math.min(tMax, steps / DOSE_SPS_MIN / 60);
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
      jogSteps: state.jogSteps,
      jogSpeed: state.jogSpeed,
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
      planVolA: state.planVolA,
      planVolB: state.planVolB,
      doseVolA: doseVolA(),
      doseVolB: doseVolB(),
    },
    switches: state.switches,
    rawSwitches: state.rawSwitches,
    ui: { stopCause: 'manual', calibPhase: state.calibPhase, motorPos: state.motorPos },
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
    case 'STOPPED':
    case 'DEBUG':
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
      state.motorPos = Math.round(state.fullPathSteps * CHARGE_AT);
      break;
    case 'DOSING': {
      state.dosing = { elapsedSec: 0, totalSec: state.doseTimeMin * 60, progress: 0, volumeA: 0, volumeB: 0 };
      dosingStartMs = Date.now();
      dosingStartPos = state.motorPos;
      const l2mm = stepsToMm(state.fullPathSteps - state.motorPos);
      state.planVolA = distToVol(state.syringeA.diameter, l2mm);
      state.planVolB = distToVol(state.syringeB.diameter, l2mm);
      break;
    }
    case 'DONE':
      state.switches.bot = (state.doneReason === 'bot');   // bot true только если реально сработал
      break;
    case 'PARKING':
      state.switches = { top: false, a: false, b: false, bot: false };
      break;
    case 'PARKED':
      state.motorPos = 0;
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
      state.calibPhase = elapsed < 1200 ? 0 : 1;   // демо: сначала «дом», потом «измерение»
      changed = true;
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
      // Как в прошивке: мотор едет к H со скоростью V = L2 / t, прогресс — по шагам.
      const L2 = Math.max(1, state.fullPathSteps - dosingStartPos);
      const speed = Math.min(Math.max(L2 / Math.max(1, state.dosing.totalSec), DOSE_SPS_MIN), MOTOR_SPS_MAX);
      const simSec = ((now - dosingStartMs) / 1000) * TIME_FACTOR;
      const traveled = Math.min(L2, Math.round(simSec * speed));
      state.motorPos = dosingStartPos + traveled;
      state.dosing.elapsedSec = Math.min(simSec, state.dosing.totalSec);
      state.dosing.progress = traveled / L2;
      state.dosing.volumeA = state.planVolA * state.dosing.progress;
      state.dosing.volumeB = state.planVolB * state.dosing.progress;
      changed = true;
      if (BOT_AT > 0 && state.dosing.progress >= BOT_AT) { state.doneReason = 'bot'; transitionTo('DONE'); return; }
      if (traveled >= L2) { state.dosing.progress = 1; state.doneReason = 'timer'; transitionTo('DONE'); return; }
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
  if (action === 'stop') return transitionTo('STOPPED');     // фаза 1: немедленный стоп
  if (action === 'reset') return transitionTo('PARKING');
  if (action === 'calibrate' && ['IDLE', 'DONE', 'PARKED'].includes(state.screen)) return transitionTo('CALIBRATING');
  if (action === 'park' && ['IDLE', 'DONE', 'STOPPED'].includes(state.screen)) transitionTo('PARKING');
  else if (action === 'start_charging' && state.screen === 'PARKED' && state.fullPathSteps > 0) transitionTo('CHARGING');
  else if (action === 'pusk' && state.screen === 'CHARGED' && state.fullPathSteps > 0) transitionTo('DOSING');
  else if (action === 'abort' && state.screen === 'DOSING') { state.doneReason = 'abort'; transitionTo('DONE'); }
  else if (action === 'new_cycle' && state.screen === 'DONE') transitionTo('IDLE');
  else if (action === 'debug' && state.screen !== 'DEBUG') transitionTo('DEBUG');
  else if (action === 'debug_back' && state.screen === 'DEBUG') transitionTo('IDLE');
  else if (action === 'jog_up' && state.screen === 'DEBUG') { state.motorPos -= state.jogSteps; broadcast(); }
  else if (action === 'jog_down' && state.screen === 'DEBUG') { state.motorPos += state.jogSteps; broadcast(); }
  else if (action === 'jog_hold_up' && state.screen === 'DEBUG') { jogHold(-1); }
  else if (action === 'jog_hold_down' && state.screen === 'DEBUG') { jogHold(+1); }
  else if (action === 'jog_stop' && state.screen === 'DEBUG') { if (state._jogTimer) { clearInterval(state._jogTimer); state._jogTimer = null; broadcast(); } }
}

// имитация удержания: двигаем непрерывно, пока не пришёл jog_stop
function jogHold(dir) {
  if (state._jogTimer) clearInterval(state._jogTimer);
  state._jogTimer = setInterval(() => {
    state.motorPos += dir * Math.max(1, Math.round(state.jogSpeed / 20));
    broadcast();
  }, 50);
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
  else if (field === 'parkSpeed') state.parkSpeed = clamp(v, 50, 6400);
  else if (field === 'chargeSpeed') state.chargeSpeed = clamp(v, 50, 6400);
  else if (field === 'fullPathSteps') state.fullPathSteps = clamp(v, 0, 5000000) | 0;
  else if (field === 'jogSteps') state.jogSteps = clamp(v, 1, 100000) | 0;
  else if (field === 'jogSpeed') state.jogSpeed = clamp(v, 50, 6400);
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
