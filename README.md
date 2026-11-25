# DOZATOR — прошивка двойного шприцевого дозатора

Прошивка для **Wemos D1 mini (ESP8266)**. Управление двойным шприцевым насосом
**через веб-интерфейс по WiFi** (мобильный, под телефон). Дисплея и энкодера нет —
устройство headless, вся индикация и управление в браузере.

## Быстрый старт

### Сборка и прошивка
```bash
pio run -t upload        # прошивка (веб-интерфейс из data/ вшит в неё при сборке)
```
`uploadfs` больше не нужен: LittleFS хранит только настройки.

### Версии и обновление по воздуху
- Версия = номер из файла `VERSION` + хеш коммита, например `1.1.0+a3f9c1e`
  («*» в конце — собрано из незакоммиченных изменений). Пишет её
  `tools/fw_version.py`. Готовые файлы для обновления после каждой сборки лежат в
  `release/`: `dozator-<версия>.bin`, `remote-<версия>.bin`.
- **Дозатор:** веб-интерфейс → «Калибровка / сервис» → «Обновить прошивку
  дозатора» (или сразу http://192.168.4.1/update), выбрать `dozator-….bin`.
  Пока мотор едет, обновление не начнётся. После записи дозатор сам перезагрузится.
- **Пульт:** зажать энкодер и СТОП на 10 с (в шапке идёт полоса «ОБНОВЛЕНИЕ»).
  Пульт поднимет свою сеть и покажет QR-код для подключения. Дальше —
  `remote/README.md`.
- Страница загрузки у обоих общая (`shared/ota_page.h`). Она проверяет файл до
  загрузки и не даст залить прошивку пульта в дозатор и наоборот.

### Подключение
- WiFi: **dozator** / пароль **dozator123** (WPA2, ≥8 символов — иначе AP не стартует)
- Открыть **http://192.168.4.1/** в браузере (на iPhone — в обычном Safari)

### Лог
```bash
pio device monitor       # 115200
```

### Выносной пульт
ESP32-C3 SuperMini с энкодером и OLED 0,96" 128×64, связь ESP-NOW. Прошивка в
`remote/firmware`, подключение и питание — `remote/README.md`, прототип меню —
`remote/prototype.html`.

## Локальный просмотр UI без микроконтроллера

Веб-интерфейс можно гонять на компьютере — мок имитирует прошивку (протокол +
автомат, срабатывание концевиков, прогресс дозирования). Нужен **Node.js ≥ 18**.

**1. Установить Node.js**

- **macOS:**
  - через [Homebrew](https://brew.sh): `brew install node`
  - либо скачать установщик с https://nodejs.org (LTS)
- **Windows:**
  - скачать установщик с https://nodejs.org (LTS) и пройти мастер
  - либо `winget install OpenJS.NodeJS.LTS`

Проверка: `node --version` (должна вывести версию).

**2. Запустить мок**

macOS / Linux (Terminal):
```bash
cd dev
npm install        # один раз — ставит зависимость ws
npm start          # → http://localhost:8000
```

Windows (PowerShell или cmd, из папки проекта):
```powershell
cd dev
npm install
npm start
```

**3. Открыть** http://localhost:8000 в браузере. Окно терминала не закрывать,
пока смотришь UI; остановить — `Ctrl+C`.

Подробности — `dev/README.md`. Папка `dev/` в прошивку не входит и на устройство
не заливается.

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
data/         – веб-интерфейс (index.html, app.css, app.js) → вшивается в прошивку (tools/embed_web.py)
tools/        – скрипты сборки: версия прошивки, вшивание веба
release/      – готовые .bin для обновления по воздуху (генерируется, не в git)
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
