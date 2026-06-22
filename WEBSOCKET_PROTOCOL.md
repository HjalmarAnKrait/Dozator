ц# WebSocket Protocol

**URL:** `ws://192.168.4.1/ws`
**Формат:** JSON, одно сообщение на WebSocket frame
**Throttle:** сервер не отправляет чаще 10 Гц (100 мс между сообщениями одного типа)
**Heartbeat:** полное состояние раз в 30 секунд при отсутствии событий

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
    "screwPitch": 2.0,
    "sleepTimeout": 15,
    "fontIndex": 1,
    "circularNav": true,
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
    "flowB": 22.6
  },
  "switches": {"top": true, "a": false, "b": false, "bot": false},
  "dosing": {
    "elapsedSec": 0,
    "totalSec": 0,
    "progress": 0,
    "volumeA": 0,
    "volumeB": 0
  },
  "ui": {
    "displaySleeping": false,
    "editingPresetIdx": -1,
    "breadcrumb": ["PARKED"]
  }
}
```

Возможные значения `screen`:
`PARKING · PARKED · CHARGING · CHARGED · DOSING · DONE ·
SERVICE_MENU · SERVICE_PITCH · SERVICE_SLEEP · SERVICE_SYRINGES · SERVICE_SYRINGE_EDIT · SERVICE_FONT`

### `error`

```json
{"type": "error", "msg": "field not allowed"}
```

---

## Client → Server

### `encoder` – имитация энкодера

```json
{"type": "encoder", "action": "cw"}
{"type": "encoder", "action": "ccw"}
{"type": "encoder", "action": "click"}
{"type": "encoder", "action": "long"}
```

### `debug_switch` – ручное управление конечниками (симулятор)

```json
{"type": "debug_switch", "switch": "a", "state": true}
```
`switch`: `"top"`, `"a"`, `"b"`, `"bot"`

### `direct_set` – прямая установка значений (desktop-режим)

```json
{"type": "direct_set", "field": "doseTimeMin", "value": 2.5}
```

| Поле | Допустимый экран |
|------|-----------------|
| `doseTimeMin` | CHARGED |
| `syringeA.presetIdx` | PARKED |
| `syringeA.diameter` | PARKED |
| `syringeB.presetIdx` | PARKED |
| `syringeB.diameter` | PARKED |
| `screwPitch` | любой |
| `sleepTimeout` | любой |
| `fontIndex` | любой |
| `circularNav` | любой |

Если поле недопустимо для текущего экрана — сервер ответит `{"type":"error"}`.

### `command` – высокоуровневые команды

```json
{"type": "command", "action": "start_charging"}
{"type": "command", "action": "enter_service"}
{"type": "command", "action": "exit_service"}
{"type": "command", "action": "pusk"}
{"type": "command", "action": "abort"}
{"type": "command", "action": "new_cycle"}
```

| Команда | Требуемый экран |
|---------|-----------------|
| `start_charging` | PARKED |
| `enter_service` | PARKED |
| `exit_service` | SERVICE_* |
| `pusk` | CHARGED |
| `abort` | DOSING |
| `new_cycle` | DONE |

---

## Поведение при отключении клиента

При закрытии вкладки / обрыве соединения сервер продолжает работу без изменений. При следующем подключении сервер немедленно отправляет полное состояние (`state`).

## Reconnect на клиенте

Клиент использует экспоненциальный backoff: 500 мс → 1 с → 2 с → 4 с → 8 с → 8 с (повтор).
