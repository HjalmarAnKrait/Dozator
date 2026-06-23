# DOZATOR — прошивка двойного шприцевого дозатора

Прошивка для **Wemos D1 mini (ESP8266)**. Управление двойным шприцевым насосом
**через веб-интерфейс по WiFi** (мобильный, под телефон). Дисплея и энкодера нет —
устройство headless, вся индикация и управление в браузере.

## Быстрый старт

### Сборка и прошивка
```bash
pio run -t upload        # прошивка
pio run -t uploadfs      # веб-интерфейс (LittleFS из data/)
```

### Подключение
- WiFi: **dozator** / пароль **dozator123** (WPA2, ≥8 символов — иначе AP не стартует)
- Открыть **http://192.168.4.1/** в браузере (на iPhone — в обычном Safari)

### Лог
```bash
pio device monitor       # 115200
```

## Локальный просмотр UI без микроконтроллера

Веб-интерфейс можно гонять на компьютере — мок имитирует прошивку (протокол +
автомат, срабатывание концевиков, прогресс дозирования):
```bash
cd dev
npm install              # один раз (ставит ws)
npm start                # → http://localhost:8000
```
Подробности — `dev/README.md`. Папка `dev/` в прошивку не входит.

## Оборудование

- PlatformIO + платформа `espressif8266`, Wemos D1 mini
- Драйвер шагового мотора STEP/DIR/EN (A4988 / DRV8825)
- 4 концевика (TOP / A / B / BOT)

Карта пинов и схема — `HARDWARE.md`.

## Структура проекта

```
src/
  app/        – AppState (g_state), StateMachine, Calculations
  hardware/   – HAL (IStepperDriver, ILimitSwitches) + Real* + Simulated*
  net/        – WiFiAp, CaptivePortal, WebServer, WsProtocol, WsBroadcaster
  storage/    – Settings (LittleFS)
  util/       – Logger, Timer, Debounce
data/         – веб-интерфейс (index.html, app.css, app.js) → LittleFS
dev/          – локальный мок UI (Node, не входит в прошивку)
include/      – config.h (GPIO, константы), version.h
docs/         – дизайн-документы
```

## Документация

- `ARCHITECTURE.md` — архитектура и ключевые решения
- `HARDWARE.md` — GPIO-карта и подключение
- `WEBSOCKET_PROTOCOL.md` — протокол WebSocket
- `src/hardware/README.md` — реальное железо vs симуляция
- `dev/README.md` — локальный мок UI
