# Architecture – DOZATOR

## Слои

```
┌─────────────────────────────────────────────────┐
│  Application layer                              │
│  AppState (g_state) · StateMachine · Calculations│
├──────────────┬───────────────┬──────────────────┤
│ OledRenderer │  EncoderInput │  WsBroadcaster   │
│ + 12 screens │  (ISR-free)   │  + WsProtocol    │
├──────────────┴───────────────┴──────────────────┤
│  HAL  IStepperDriver · ILimitSwitches           │
├───────────────────┬─────────────────────────────┤
│ SimulatedStepper  │ SimulatedSwitches            │
│ (RealStepper TODO)│ (RealSwitches  TODO)         │
├───────────────────┴─────────────────────────────┤
│  WiFiAp · CaptivePortal · AsyncWebServer        │
└─────────────────────────────────────────────────┘
```

## Поток событий

```
Энкодер (GPIO ISR) ──┐
                      ├──► EncoderInput::queue ──► StateMachine::onEvent()
WebSocket (encoder)  ─┘                                  │
                                                          ▼
                                              g_state изменяется
                                                          │
                      ┌───────────────────────────────────┤
                      ▼                                   ▼
              OledRenderer::render()          WsBroadcaster::broadcastNow()
              (20 Hz из loop())               (throttle 100 ms)
```

## Ключевые решения

### Один глобальный AppState
`g_state` — единственный источник правды. Доступен только из главного цикла (не из ISR). Encoder библиотека Stoffregen обрабатывает ISR внутри, в `EncoderInput::tick()` читается уже вычисленное смещение.

Новые поля: `uint8_t fontIndex` (0–3, по умолчанию 1 = S), `bool circularNav` (по умолчанию `true`). Оба персистируются в Settings JSON под ключами `fontIndex` и `circularNav`.

### HAL через интерфейсы
`IStepperDriver` и `ILimitSwitches` — чистые интерфейсы. В текущей сборке используются `SimulatedStepper` и `SimulatedSwitches`. Переключение на реальное железо: изменить два `#define` и реализовать `Real*` методы.

### SimulatedSwitches
Хранит ссылку на экран через `g_state.screen`. При смене экрана вызывается `notifyScreenChange()`, что сбрасывает таймеры симуляции. Метод `debugSetSwitch()` позволяет веб-панели вручную форсировать конечники.

Исправлен баг переполнения: в `tick()` добавлена защита `if (nowMs < m_screenEnterMs) return;` (аналогично проверке в sleep-логике).

### Throttled WebSocket broadcast
`WsBroadcaster::requestBroadcast()` помечает "нужна рассылка". В `tick()` реально отправляется не чаще 10 Гц (100 мс). Heartbeat каждые 30 сек независимо от событий.

### Нет `delay()` в loop()
Все таймеры — на `millis()`. Единственный `delay(1000)` — в `showSplash()` при запуске, до того как запустился loop().

### LittleFS вместо SPIFFS
SPIFFS deprecated. Файловая система `littlefs` задана в `platformio.ini`. Веб-файлы заливаются командой `pio run -t uploadfs`.

### ArduinoJson v7
Используется `JsonDocument` (динамический). В горячих путях (broadcast) размер буфера 1200 байт фиксированный (`char m_buf[1200]`).

### Без `String` в горячих путях
Рендеринг экранов использует `char[]` + `snprintf`. `String` (Arduino) не используется в render/serialize путях.

### navWrap — кольцевая/ограниченная навигация
`StateMachine::navWrap(cur, dir, total)`:
- если `g_state.circularNav == true` → `(cur + dir + total) % total`
- иначе → `constrain(cur + dir, 0, total - 1)`

SERVICE_MENU всегда использует кольцевую навигацию независимо от флага (чтобы пункт «Назад» был всегда достижим).

### uint32_t underflow guard
В проверке времени сна (`millis() - lastInteractionMs > sleepTimeout`) добавлена защита `if (nowMs < lastInteractionMs) return;` на случай переполнения `uint32_t`. Аналогичная защита в `SimulatedSwitches::tick()`.

### Направление энкодера
CW и CCW поменяны местами в `EncoderInput.cpp` — исправлена инверсия физического направления вращения.

## FontConfig и адаптивная вёрстка

**Файлы:** `src/ui/FontConfig.h`, `src/ui/FontConfig.cpp`

Структура `FontTier`:
```cpp
struct FontTier {
    const uint8_t* mainFont;
    const char*    name;
    uint8_t        charW;
    uint8_t        rowH;
    uint8_t        itemsPerPage;
    uint8_t        rows[8];  // ROW0..ROW7 — Y-координаты строк
};
```

Четыре тира:

| Индекс | Имя | Шрифт    | charW | rowH | itemsPerPage |
|--------|-----|----------|-------|------|--------------|
| 0      | XS  | 4×6      | 4     | 6    | 7            |
| 1      | S   | 6×13     | 6     | 13   | 4 (default)  |
| 2      | M   | 7×13     | 7     | 13   | 4            |
| 3      | L   | 9×15     | 9     | 15   | 3            |

Функция `activeTier()` (inline) возвращает `g_fontTiers[g_state.fontIndex]`.

Все макросы в `Fonts.h` / `ScreenHelper.h` (`ROW0`–`ROW4`, `CHAR_W`, `ROW_H`, `ITEMS_PER_PAGE`, `PAGE_ROWS`) разворачиваются в вызовы `activeTier().xxx`. Рендереры экранов адаптируются автоматически — смена шрифта не требует правок в коде экранов.

## Пагинация экранов

Экраны с количеством пунктов больше `ITEMS_PER_PAGE` разбиваются на страницы.

```
page = cursorIndex / ITEMS_PER_PAGE
firstVisible = page * ITEMS_PER_PAGE
```

В правом нижнем углу `drawPageIndicator()` выводит `"X/Y"` шрифтом FONT_SMALL (только если страниц > 1).

Экраны с пагинацией:

| Экран            | Пунктов | Страниц (шрифт S) |
|------------------|---------|-------------------|
| PARKED           | 6       | 2                 |
| SERVICE_MENU     | 6       | 2                 |
| SERVICE_SYRINGES | переменно | зависит от числа шприцов |
| SERVICE_FONT     | 5       | 2                 |

## Breadcrumb навигация сервисного меню

```
PARKED → [push {PARKED,5}] → SERVICE_MENU
        → [push {SERVICE_MENU,1}] → SERVICE_SYRINGES
                → [push {SERVICE_SYRINGES,0}] → SERVICE_SYRINGE_EDIT
                ← [pop] → SERVICE_SYRINGES (cursor=0)
        ← [pop] → SERVICE_MENU (cursor=1)
        → [push {SERVICE_MENU,3}] → SERVICE_FONT
        ← [pop] → SERVICE_MENU (cursor=3)
← [pop] → PARKED (cursor=5)
```

Пункты SERVICE_MENU (6 штук, индексы 0–5):
`Шаг винта / Шприцы / Сон экрана / Шрифт / Кольцо (toggle) / Назад`

## Sleep

- Разрешён только в: PARKED, CHARGED, DONE, SERVICE_*
- Запрещён в: PARKING, CHARGING, DOSING (идёт активный процесс)
- При любом событии энкодера: `lastInteractionMs` обновляется
- Первое событие после сна — только будит, не обрабатывается
- Веб-интерфейс продолжает работать пока OLED спит
- Защита от `uint32_t` underflow: `if (nowMs < lastInteractionMs) return;`

## Экраны (12 штук)

| Screen enum       | Назначение                                      |
|-------------------|-------------------------------------------------|
| SPLASH            | Заставка при старте                             |
| PARKED            | Главное меню (позиция 0, выбор операции)        |
| PARKING           | Анимация возврата к позиции 0                   |
| CHARGING          | Набор шприца (чекбоксы 16×16, толстая галочка)  |
| CHARGED           | Шприц набран; нет заголовка, подсказка диапазона|
| DOSING            | Процесс дозирования                             |
| DONE              | Инвертированный статус-бар "ВЫПОЛНЕНО", статы   |
| SERVICE_MENU      | Сервисное меню (6 пунктов, кольцевая навигация) |
| SERVICE_SYRINGES  | Список шприцов                                  |
| SERVICE_SYRINGE_EDIT | Редактирование параметров шприца             |
| SERVICE_SLEEP     | Настройка тайм-аута сна                         |
| SERVICE_FONT      | Выбор шрифта (5 пунктов, 4 тира + Назад)        |
