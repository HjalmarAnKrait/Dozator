# DOZATOR – прошивка шприц-дозатора двойного

Прошивка для Wemos D1 mini (ESP8266): управление двойным шприцевым насосом с OLED-дисплеем, энкодером и веб-интерфейсом через WiFi.

## Быстрый старт

### 1. Сборка и прошивка

```bash
pio run -t upload        # прошивка firmware
pio run -t uploadfs      # загрузка LittleFS (веб-файлы)
```

### 2. Подключение

- WiFi: **dozator** / пароль **123321**
- Captive portal открывается автоматически на телефоне
- Или вручную: `http://192.168.4.1/`
- Desktop-режим: `http://192.168.4.1/?layout=desktop`

### 3. Лог

```bash
pio device monitor
```

## Требования

- PlatformIO + платформа `espressif8266`
- Wemos D1 mini (или совместимая плата)
- OLED SSD1306 128×64 I2C
- Ротационный энкодер с кнопкой

## Структура проекта

```
src/          – исходный код прошивки
  app/        – AppState, StateMachine, Calculations
  hardware/   – HAL интерфейсы + симуляторы + реальные заглушки
  input/      – EncoderInput
  net/        – WiFi AP, CaptivePortal, WebServer, WebSocket
  storage/    – Settings (LittleFS)
  ui/         – OLED рендерер + 12 экранов + FontConfig
  util/       – Logger, Timer, Debounce
data/         – веб-файлы (index.html, app.css, app.js)
include/      – config.h (GPIO, константы), version.h
```

## Документация

- `ARCHITECTURE.md` – архитектурные решения
- `HARDWARE.md` – GPIO-карта
- `WEBSOCKET_PROTOCOL.md` – протокол WebSocket
- `src/hardware/README.md` – как переключить на реальное железо
