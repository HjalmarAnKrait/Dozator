# Architecture – DOZATOR

Headless-устройство: ни OLED, ни энкодера. Единственный UI — веб-интерфейс по
WebSocket. Прошивка управляется командами с веба и переходами по концевикам.

## Слои

```
┌─────────────────────────────────────────────────┐
│  Application layer                                │
│  AppState (g_state) · StateMachine · Calculations │
├──────────────────────────┬───────────────────────┤
│  WsProtocol              │  WsBroadcaster        │
│  (разбор команд, state)  │  (throttle 10 Гц)     │
├──────────────────────────┴───────────────────────┤
│  HAL  IStepperDriver · ILimitSwitches             │
├───────────────────────┬───────────────────────────┤
│ RealStepper (timer1)   │ RealSwitches (debounce)  │
│ SimulatedStepper       │ SimulatedSwitches        │
├───────────────────────┴───────────────────────────┤
│  WiFiAp · CaptivePortal · AsyncWebServer          │
└─────────────────────────────────────────────────┘
```

Переключение симуляция/железо — один `#define USE_SIMULATED_HW` в `config.h`.

## Поток управления

```
Браузер (WebSocket)
   │  command / direct_set / set_preset
   ▼
WsProtocol::handleMessage ──► StateMachine::transitionTo / g_state
                                          │
   концевики (RealSwitches) ──► StateMachine::tick ──► переходы по цикл-стадиям
                                          │
                                          ▼
                                 g_state изменяется
                                          │
                                          ▼
                         WsBroadcaster (throttle 100 мс) ──► все клиенты (state)
```

- **Команды** (`start_charging`, `pusk`, `abort`, `new_cycle`) → прямые переходы.
- **direct_set / set_preset** → правка значений в `g_state` + ребродкаст.
- **Концевики** опрашиваются в `StateMachine::tick()`; они двигают цикл
  (PARKING→PARKED по TOP, CHARGING→CHARGED по A&B, DOSING→DONE по BOT/таймеру).

## Цикл состояний

```
PARKING ──TOP──► PARKED ──[start_charging]──► CHARGING ──A&B──► CHARGED
   ▲                                                              │
   └──────────[new_cycle]── DONE ◄──BOT/таймер── DOSING ◄─[pusk]──┘
```

`Screen` enum содержит только эти 6 стадий (сервисных экранов нет — настройки
задаются через `direct_set`/`set_preset` в любой момент).

## Ключевые решения

### Один глобальный AppState
`g_state` — единственный источник правды, доступен только из главного цикла
(не из ISR). Часть полей осталась вестигиальной после удаления OLED/энкодера
(`cursorIndex`, `editing`, `fontIndex`, `circularNav`, `breadcrumb`) — на работу
не влияют, при желании дочищаются.

### HAL через интерфейсы
`IStepperDriver` и `ILimitSwitches` — чистые интерфейсы. В реальной сборке
(`USE_SIMULATED_HW 0`) используются `RealStepper` и `RealSwitches`; в симуляции —
`Simulated*` (автоматически прогоняют цикл по таймерам, для тестов без железа).

### RealStepper — шаги через timer1
ESP8266 не успевает дёргать STEP в `loop()` на высоких частотах, поэтому импульсы
генерит аппаратный таймер `timer1` (ISR в IRAM, прямой доступ к GPIO через
`GPOS`/`GPOC`). `moveTo(target, speed)` задаёт направление и интервал; ISR в
«toggle»-режиме считает шаги. EN — active-LOW.

### RealSwitches — дебаунс
4 концевика на прямых GPIO (`INPUT_PULLUP`, замыкание на GND = LOW), дебаунс
30 мс (`util/Debounce.h`). `tick()` читает пины, `read()` отдаёт снимок.

### rawSwitches — живая отладка концевиков
`StateMachine::tick()` пишет реальные показания `ILimitSwitches::read()` в
`g_state.rawSwitches` и шлёт бродкаст при изменении. UI рисует их в постоянной
панели TOP/A/B/BOT — видно на любом экране, независимо от стадии.

### Throttled WebSocket broadcast
`WsBroadcaster::requestBroadcast()` помечает «нужна рассылка»; реально шлётся не
чаще 10 Гц (100 мс). Heartbeat (полный `state`) каждые 30 с.

### Расчёты (Calculations)
`flowA = volA/doseTime`; `flowB = flowA·(areaB/areaA)`; линейная скорость винта
`vScrew = flowA·1000/areaA` (мл→мм³); `motorRpm = vScrew/pitch`.
`timeRangeMin()` — физически допустимый диапазон времени дозирования из пределов
потока (`MIN/MAX_FLOW_MLPM`) и оборотов (`MOTOR_RPM_MIN/MAX`); отдаётся в UI как
`cycle.timeMin/timeMax`.

> Важно: множитель ×1000 (мл→мм³) обязателен — без него `motorRpm` занижался в
> 1000 раз (мотор почти не крутился). `flowB` от этого не зависит (отношение
> площадей сокращает множители).

### Прочее
- Нет `delay()` в `loop()` — всё на `millis()`.
- LittleFS (не SPIFFS); веб-файлы — `pio run -t uploadfs`.
- ArduinoJson v7 (`JsonDocument`), фиксированный буфер бродкаста.
- Captive-portal: на любой неизвестный путь отдаётся сама страница (HTTP 200),
  без 302-редиректов (иначе Safari ловил ERR_TOO_MANY_REDIRECTS).

## Настройки (persist в LittleFS)
`screwPitch`, `sleepTimeout`, пресеты шприцов. Меняются через `direct_set`
(шаг/сон) и `set_preset` (пресеты) в любой момент, сохраняются с дебаунсом 2 с.

## Локальный мок (dev/)
`dev/mock-server.js` — Node HTTP+WS, повторяет протокол и автомат, чтобы гонять UI
без платы. Расчёты портированы из `Calculations.cpp`. При изменении протокола в
прошивке — синхронизировать мок.
