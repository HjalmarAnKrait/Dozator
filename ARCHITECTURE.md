# Architecture – DOZATOR

Headless-устройство: ни OLED, ни энкодера на самой плате. UI — веб-интерфейс по
WebSocket и выносной пульт по ESP-NOW (`remote/`). Прошивка управляется командами
с веба и пульта и переходами по концевикам.

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
IDLE ──[park]──► PARKING ──TOP──► PARKED ──[start_charging]──► CHARGING ──A&B──► CHARGED
 ▲                                                                                  │
 └────────────────[new_cycle]── DONE ◄──BOT/таймер── DOSING ◄────────────[pusk]─────┘
```

При включении — `IDLE` (ничего не двигается). Парковка стартует только по команде
`park` (кнопка в UI), не автоматически. `Screen` enum: IDLE + 6 стадий цикла
(сервисных экранов нет — настройки через `direct_set`/`set_preset` в любой момент).

## Выносной пульт (ESP-NOW)
`net/EspNowLink` принимает пакеты пульта (протокол — `shared/RemoteProtocol.h`, общий
с прошивкой пульта в `remote/firmware`) и вызывает те же обработчики, что и веб
(`WsProtocol::handleCommand/handleDirectSet/handleSetPreset`), так что правила
«какая команда в каком состоянии» одни. Состояние уходит пульту пакетом STATE:
10 Гц при движении, 2 Гц в покое и сразу после изменений, но только пока пульт
присылает HELLO (не дольше 3 с после последнего). WiFi работает в AP+STA на
фиксированном канале `RP_CHANNEL` — на нём же пульт.

## Ключевые решения

### transitionTo — это вход в состояние
`StateMachine::transitionTo()` запускает движение и сторожа. После правки значений
(`direct_set`, пресеты) вызывается только `requestBroadcast()`. Раньше
`direct_set` делал `transitionTo(g_state.screen)`, и правка любой настройки
перезапускала дозирование или калибровку.

### Защита хода
- Ход к концевику ограничен расстоянием: парковка — H + 30 % (не меньше +20 мм),
  до калибровки и в калибровке — `MAX_TRAVEL_MM` (200 мм). Доехали до предела без
  концевика — `STOPPED`. Таймаут по времени (ход / скорость × 1,5 + 5 с) — страховка.
- Зарядка едет не дальше H; нажался BOT или доехали до H без A и B — `STOPPED`
  (`stuck_charge`).
- ПУСК при позиции заряда ≥ H — `STOPPED` (`bad_charge`), а не ложное «Готово».
- Калибровка BOT: быстро до концевика, отъезд на `CALIB_BACKOFF_MM` (2 мм),
  медленный подход `CALIB_SLOW_MM_S` (0,5 мм/с) — точка срабатывания не зависит от
  выбега на скорости парковки. BOT не отпустился после отъезда или не сработал на
  обратном подходе — `calib_fail`. H хранится на `CALIB_END_MARGIN_MM` (0,5 мм)
  короче точки BOT: доза заканчивается по ходу, BOT остаётся страховкой.
- Мотор: скорость до `MOTOR_RPM_MAX` (120 об/мин = 6400 шаг/с), до
  `MOTOR_START_RPM` (15 об/мин) трогается сразу, выше — разгон и торможение перед
  целью с `MOTOR_ACCEL_RPS2` (2 об/с²). Разворот на ходу — через торможение. На
  концевике и по СТОП — мгновенный стоп. Разгон считает `RealStepper::tick()`, ISR
  только подхватывает новый период.
- Ручной режим: концевик в сторону движения останавливает мотор; удержание
  требует повтора команды раз в 300 мс (deadman 1 с), обрыв WebSocket — стоп.
- Концевики: нажатие подтверждается за 5 мс (каретка не проезжает точку
  срабатывания), отпускание — за 30 мс.


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
нажатия 5 мс и отпускания 30 мс (`util/Debounce.h`). `tick()` читает пины,
`read()` отдаёт снимок.

### rawSwitches — живая отладка концевиков
`StateMachine::tick()` пишет реальные показания `ILimitSwitches::read()` в
`g_state.rawSwitches` и шлёт бродкаст при изменении. UI рисует их в постоянной
панели TOP/A/B/BOT — видно на любом экране, независимо от стадии.

### Throttled WebSocket broadcast
`WsBroadcaster::requestBroadcast()` помечает «нужна рассылка»; реально шлётся не
чаще 10 Гц (100 мс). Heartbeat (полный `state`) каждые 30 с.

### Расчёты (Calculations)
Объём дозы `doseVolA`: в CHARGED — по оставшемуся ходу H − L (столько реально
выдавится), в DOSING/DONE — плановый объём, иначе — полный объём пресета.
`flowA = doseVolA/doseTime`; `flowB = flowA·(areaB/areaA)`; линейная скорость винта
`vScrew = flowA·1000/areaA` (мл→мм³); `motorRpm = vScrew/pitch`.
`timeRangeMin()` — допустимый диапазон времени дозирования: снизу — поток
`MAX_FLOW_MLPM` и скорость мотора `MOTOR_SPS_MAX`, сверху — `DOSE_TIME_MAX_MIN`
(20 мин) и `DOSE_SPS_MIN` (1 шаг/с). Нижнего предела оборотов нет: шаговый мотор
ровно идёт и на единицах шагов в секунду. Отдаётся в UI как `cycle.timeMin/timeMax`.

> Важно: множитель ×1000 (мл→мм³) обязателен — без него `motorRpm` занижался в
> 1000 раз (мотор почти не крутился). `flowB` от этого не зависит (отношение
> площадей сокращает множители).

### Прочее
- Нет `delay()` в `loop()` — всё на `millis()`.
- LittleFS (не SPIFFS) — только настройки. Веб-файлы из `data/` вшиваются в прошивку
  (gzip, `tools/embed_web.py`) и отдаются из флеша программы; обновление — один `.bin`.
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
