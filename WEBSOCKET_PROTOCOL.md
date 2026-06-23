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
  "settings": {
    "screwPitch": 2.0,
    "sleepTimeout": 15,
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
    "flowB": 22.6,
    "timeMin": 0.12,
    "timeMax": 3.53
  },
  "switches":    {"top": true, "a": false, "b": false, "bot": false},
  "rawSwitches": {"top": true, "a": false, "b": false, "bot": false},
  "dosing": {
    "elapsedSec": 0, "totalSec": 0, "progress": 0, "volumeA": 0, "volumeB": 0
  }
}
```

- `screen`: `PARKING · PARKED · CHARGING · CHARGED · DOSING · DONE`.
- `switches` — сценарное состояние стадии (что «должно быть»).
- **`rawSwitches` — живые показания концевиков** (`ILimitSwitches::read()`),
  обновляются при любом изменении. Использовать для постоянной отладочной
  индикации в UI, независимо от экрана.
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
| `start_charging` | PARKED  | → CHARGING |
| `pusk`           | CHARGED | → DOSING |
| `abort`          | DOSING  | → DONE |
| `new_cycle`      | DONE    | → PARKING |

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

Если поле недопустимо для текущего экрана — сервер ответит `{"type":"error"}`.

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
