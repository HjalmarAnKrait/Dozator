# Hardware Layer – How to Switch from Simulation to Real Iron

## Current state

`USE_SIMULATED_HW = 0` — **реальное железо**. Прошивка использует `RealStepper`
(шаги через timer1-ISR) и `RealSwitches` (4 концевика на прямых GPIO). Дисплей
и энкодер отключены (`USE_OLED`/`USE_ENCODER = 0`), управление — через веб.

Пины — см. `HARDWARE.md §2.1`:
- Мотор: STEP=D5/GPIO14, DIR=D0/GPIO16, EN=D3/GPIO0 (active-LOW, pull-up на D3).
- Концевики TOP/A/B/BOT: D1/D2/D6/D7, все `INPUT_PULLUP`, замыкание на общий GND.

Чтобы вернуться к симуляции — поставьте `USE_SIMULATED_HW = 1`.

## (Историческая справка) Switching to real hardware

### Step 1 – Wire the hardware

See `HARDWARE.md` for the GPIO map and schematic notes.

### Step 2 – Implement `RealStepper`

Open `src/hardware/RealStepper.cpp` and fill in each `TODO_HW` method:

| Method | What to do |
|---|---|
| `moveTo(target, speed)` | Generate STEP pulses on `STEP_STEP_PIN` at `speed` Hz; set `STEP_DIR_PIN` before first pulse |
| `stop()` | Stop pulse generation immediately |
| `currentPosition()` | Return ISR-maintained step counter |
| `isBusy()` | Return `true` while moving |
| `enable()` | `digitalWrite(STEP_EN_PIN, LOW)` |
| `disable()` | `digitalWrite(STEP_EN_PIN, HIGH)` |

### Step 3 – Implement `RealSwitches`

Open `src/hardware/RealSwitches.cpp` and fill in:

- `tick()` – read SW_TOP from `SW_TOP_PIN` with 30 ms debounce; read A/B/BOT
  from I2C GPIO expander (MCP23017 or PCF8574).
- `read()` – return the last debounced snapshot.

### Step 4 – Switch the define

In `include/config.h`:

```c
#define USE_SIMULATED_HW  0   // was 1
#define SIM_TIME_FACTOR   1   // real-time dosing
```

### Step 5 – Rebuild and flash

```
pio run -t upload
pio run -t uploadfs
```

### Hardware test checklist

- [ ] SW_TOP triggers PARKING → PARKED transition within 100 ms of contact
- [ ] SW_A fires during charging → indicator lights on OLED
- [ ] SW_B fires → CHARGED screen appears
- [ ] Motor moves in correct direction; STEP/DIR waveform visible on scope
- [ ] Enable pin is asserted before motion, deasserted after DONE
- [ ] SW_BOT stops dosing
