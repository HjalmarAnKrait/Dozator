# WebSocket Protocol

**URL:** `ws://192.168.4.1/ws`
**Формат:** JSON, одно сообщение на WebSocket frame
**Throttle:** сервер не отправляет чаще 10 Гц (100 мс между сообщениями одного типа)
**Heartbeat:** полное состояние раз в 30 секунд при отсутствии событий

> Версия headless: энкодер и OLED удалены. Управление — только `command` +
> `direct_set` + `set_preset`. Сообщение `encoder` и экраны `SERVICE_*` убраны.

---

## Server → Client

### `state` – полное состояние (при каждом изменении и при подключении)

```json
{
  "type": "state",
  "screen": "PARKED",
  "cursor": 0,
  "editing": false,
  "settings": {
    "screwPitch": 8.0,
    "sleepTimeout": 15,
    "parkSpeed": 800,
    "chargeSpeed": 400,
    "fullPathSteps": 120000,
    "jogSteps": 200,
    "jogSpeed": 800,
    "presets": [
      {"vol": 10, "diam": 14.5},
      {"vol": 20, "diam": 19.0},
      {"vol": 50, "diam": 28.6}
    ]
  },
  "cycle": {
    "syringeA": {"presetIdx": 1, "diameter": 19.0, "volume": 20},
    "syringeB": {"presetIdx": 2, "diameter": 28.6, "volume": 50},
    "doseTimeMin": 2.0,
    "flowA": 10.0,
    "flowB": 22.66,
    "planVolA": 0,
    "planVolB": 0,
    "doseVolA": 20,
    "doseVolB": 50,
    "timeMin": 0.073,
    "timeMax": 20
  },
  "switches":    {"top": true, "a": false, "b": false, "bot": false},
  "rawSwitches": {"top": true, "a": false, "b": false, "bot": false},
  "dosing": {
    "elapsedSec": 0, "totalSec": 0, "progress": 0, "volumeA": 0, "volumeB": 0,
    "reason": "timer"
  },
  "ui": {
    "displaySleeping": false, "editingPresetIdx": -1,
    "remoteOnline": true, "remoteFw": "0.3.0", "remoteProto": 0, "proto": 5,
    "fw": "1.1.0", "ota": false, "calibPhase": 0, "motorPos": 0,
    "stopCause": "manual", "breadcrumb": []
  }
}
```

- `screen`: `IDLE · PARKING · PARKED · CHARGING · CHARGED · DOSING · DONE`.
- `switches` — сценарное состояние стадии (что «должно быть»).
- **`rawSwitches` — живые показания концевиков** (`ILimitSwitches::read()`),
  обновляются при любом изменении. Использовать для постоянной отладочной
  индикации в UI, независимо от экрана.
- `cycle.doseVolA` / `cycle.doseVolB` — объём дозы, мл: в CHARGED по оставшемуся
  ходу H − L, в DOSING/DONE плановый, иначе полный объём пресета. `flowA/flowB`
  и диапазон времени считаются от него.
- `cycle.timeMin/timeMax` — допустимое время дозы, мин: снизу — скорость мотора
  6400 шаг/с, сверху — 20 мин (и не медленнее 1 шаг/с). Пример — 20 мл при Tr8x8.
- `ui.remoteOnline` — выносной пульт на связи (присылал HELLO последние 3 с).
- `ui.stopCause`: `manual · stuck_home · stuck_charge · stuck_end · calib_fail ·
  bad_charge` (заряд ниже H — дозировать нечего).
- `cycle.timeMin` / `cycle.timeMax` — физически допустимый диапазон времени
  дозирования (мин) под текущие объём/диаметр/шаг (пределы потока и оборотов
  мотора). UI валидирует `doseTimeMin` по нему.

### `error`

```json
{"type": "error", "msg": "field not allowed"}
```

---

## Client → Server

### `command` – высокоуровневые команды

```json
{"type": "command", "action": "start_charging"}
```

| Команда | Требуемый экран | Переход |
|---------|-----------------|---------|
| `reset`          | любой   | → PARKING (стоп + хоуминг) |
| `park`           | IDLE / DONE | → PARKING |
| `start_charging` | PARKED  | → CHARGING |
| `pusk`           | CHARGED | → DOSING |
| `abort`          | DOSING  | → DONE |
| `new_cycle`      | DONE    | → IDLE |

При включении устройство в `IDLE` — ничего не двигается; парковка стартует только
по команде `park` (кнопка «Парковка» в UI).

### `direct_set` – прямая установка значений

```json
{"type": "direct_set", "field": "doseTimeMin", "value": 2.5}
```

| Поле | Допустимый экран |
|------|-----------------|
| `doseTimeMin` | CHARGED |
| `syringeA.presetIdx` / `syringeA.diameter` | PARKED |
| `syringeB.presetIdx` / `syringeB.diameter` | PARKED |
| `screwPitch` | любой |
| `sleepTimeout` | любой |
| `parkSpeed` (шаг/с, 50–6400) | любой |
| `chargeSpeed` (шаг/с, 50–6400) | любой |

Если поле недопустимо для текущего экрана — сервер ответит `{"type":"error"}`.
Правка значения не перезапускает текущее состояние: сервер только рассылает новое `state`.

### Ручной режим (DEBUG)

| Команда | Действие |
|---------|----------|
| `debug` / `debug_back` | войти (из любого экрана) / выйти в IDLE |
| `jog_up` / `jog_down` | сдвиг на `jogSteps` |
| `jog_hold_up` / `jog_hold_down` | движение до `jog_stop`; **клиент повторяет команду каждые 300 мс**, без повтора дольше 1 с мотор встаёт сам |
| `jog_stop` | стоп |

Концевик TOP останавливает движение вверх, BOT — вниз. Отключение WebSocket-клиента
останавливает движение.

### `set_preset` – редактирование пресета шприца

```json
{"type": "set_preset", "index": 0, "vol": 10, "diam": 14.5}
```

Меняет `presets[index]` (vol 1..500 мл, diam 1..150 мм), сохраняется в флеш.
Если выбранный шприц A/B ссылается на этот пресет — его диаметр синхронизируется.

### `debug_switch` – ручное управление концевиками (только симулятор)

```json
{"type": "debug_switch", "switch": "a", "state": true}
```
`switch`: `"top"`, `"a"`, `"b"`, `"bot"`. На реальном железе игнорируется.

---

## Reconnect на клиенте

Экспоненциальный backoff: 500 мс → 1 с → 2 с → 4 с → 8 с (повтор).
При переподключении сервер немедленно отправляет полное `state`.
