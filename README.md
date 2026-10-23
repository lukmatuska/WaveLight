# WaveLight

A gesture-controlled night light. Wave your hand over it to turn it on or off,
hold your hand over it to set the flame size, and double-wave to pick a color.
The light is an animated candle flame on WS2812 LEDs. It runs on a Li-ion
battery and sleeps when nobody is around.

- **MCU:** ATtiny416 (4 kB flash, 256 B RAM), internal 16 MHz oscillator
- **Sensor:** ams TMF8801 time-of-flight distance sensor
- **LEDs:** 4× WS2812B (count is configurable)
- **Firmware:** C, MPLAB XC8 (free license), in [`FW/`](FW/)
- **Hardware:** KiCad project in the repository root

---

## Using it

| Gesture | What it does |
|---|---|
| **Wave** (hand over the sensor and away again, under 0.6 s) | Light on / off. Reacts ~0.5 s after the hand leaves, because it waits to see if a second wave follows |
| **Hold + move** (hand stays over the sensor, then moves) | Flame size: closer = smaller, dimmer embers; farther = bigger, brighter flame. Saved when the hand leaves |
| **Wave, then hold right away** | Turns on, then sets the flame size |
| **Double wave** | Color mode: the light dips briefly. Bring your hand back within 2 s and the color follows its distance. Take the hand away, and after 1 s the light dips again and the color is saved |

**Distances:** hands are detected up to **60 cm**. Size and color are set
between about **6 cm** (closest) and **32 cm** (farthest).

**Colors**, from close to far in color mode:

| Hand distance | ~6 cm | | | | | | | | | ~32 cm |
|---|---|---|---|---|---|---|---|---|---|---|
| Color | red | orange | yellow | green | cyan | blue | magenta | red | warm white (2700 K) | cool white (6500 K) |

So the strip goes once around the color wheel back to red, then into warm
white and cools down to 6500 K. The hotter parts of the flame shift a little
further along the strip, which gives the flame its lighter tips.

### Status signals

| LEDs | Meaning |
|---|---|
| Brief dip to dark and back | Color mode started / ended |
| Magenta for 1 s at power-on | Sensor calibrated and calibration stored (first boot after flashing) |
| Red blinking continuously | Sensor not responding. Retries every ~8 s (watchdog reset) |
| 2 red blinks when turning on | Battery low (< 3.5 V), turns on anyway |
| 3 red blinks, stays off | Battery critical (< 3.3 V). Charge above 3.5 V to use again |

The battery signals only appear if the battery check is enabled (see
[Configuration](#configuration)).

### Things that might surprise you

- **Static objects are learned.** Anything that stays still for 10 s becomes
  background. A hand only counts if it is at least 8 cm closer than the
  background, so you can wave between the lamp and e.g. a shelf. After you
  place or remove something, give it ~10 s to adapt.
- **Placed objects never change the settings.** A hold only adjusts the flame
  once the hand has *moved* 3 cm, and objects don't move.
- **Pulling your hand away doesn't change the setting.** On release the
  value from ~200 ms earlier is kept.
- **Waking from idle:** after 15 s with the light off, the sensor slows down
  to save power. The first wave still works, but a very quick flick can be
  missed.

---

## Hardware

### Pinout

| ATtiny416 pin | Function |
|---|---|
| PB3 | WS2812 data (LED strip connector J2; also gate of Q1 for the alternative power LED on J3) |
| PB1 / PB0 | I²C SDA / SCL to the TMF8801 (internal pull-ups, no external resistors fitted) |
| PC0 | TMF8801 INT (open drain, internal pull-up) - wakes the MCU |
| PC1 | TMF8801 EN |
| PB4 / PB2 | TMF8801 GPIO0 / GPIO1 (unused, driven low by the sensor) |
| PA1 | Battery voltage measurement (divider middle) |
| PC3 | Battery divider low side - pulled low only while measuring |
| PA0 | UPDI programming |

### Board

- **Power:** Li-ion battery (J6) → power switch (SW1) → TCR2L LDO → VDD for
  the MCU and sensor. The LED strip runs directly from the battery voltage.
- **Charging:** USB-C (J7) or the jack tip → charger IC (U3, charge current set
  by R3). The charger's battery output is on the switched side of SW1, so the
  battery only charges while the switch is on.
- **3.5 mm jack (J1):** tip = charger input, ring = UPDI, sleeve = GND. It is
  used for programming.
- **J4 / J5:** sensor signal headers (VDD, GPIO0/1, SDA, SCL, INT, EN, GND).
- **JP1:** solder jumper EN → GND (holds the sensor off when closed).
- **Battery divider:** R2 1 MΩ (battery → PA1) and R4 470 kΩ (PA1 → PC3).

### Known hardware limitations

- **No I²C pull-ups.** The internal ones are used and the bus runs at 50 kHz.
  Add 4.7 kΩ to VDD on SDA/SCL in the next revision.
- **The WS2812s draw ~0.5-1 mA each even when dark** and are powered straight
  from the battery, so the firmware can't switch them off. A high-side switch
  on the strip power would be the biggest idle-current saving.
- **Sensor range is ~64 cm** with the firmware built into the sensor (see
  [Sensor range](#sensor-range)).

---

## Firmware

### Files

| File | Contents |
|---|---|
| [`main.c`](FW/main.c) | Configuration, startup, main loop, gesture → action mapping, sleep, EEPROM settings |
| [`gesture.h`](FW/gesture.h) | Wave / double wave / hold / tracking detection, background learning |
| [`light.h`](FW/light.h) | Flame animation, colors, fades, gamma correction, auto-off |
| [`tmf8801.h`](FW/tmf8801.h) | TMF8801 driver: power-up, factory calibration, measurement, results |
| [`battery.h`](FW/battery.h) | Battery voltage check (ADC) |
| [`twi.h`](FW/twi.h) | I²C master driver |
| [`ws2812b.h`](FW/ws2812b.h) | Cycle-counted WS2812 driver (16 MHz) |
| [`PATCHING.md`](FW/PATCHING.md) | How to upload the TMF8801 RAM patch after switching to an ATtiny1616 |

### Building and flashing

1. Open `FW/` in VS Code with the MPLAB extensions (project file
   `FW/.vscode/Wavelight-firmware.mplab.json`, XC8 v3.00, `ATtiny_DFP` 3.4.278).
2. Build. The optimization level in the project doesn't matter: `main.c`
   forces `-O2` without inlining with a `#pragma`. At `-O0` the firmware
   doesn't fit. The free XC8 license allows `-O2` but not `-Os`.
3. Flash over UPDI (the project uses an MPLAB Snap).
4. **Set the fuse `OSCCFG` to 16 MHz** (`FREQSEL_16MHZ`) on every new chip.
   The factory default is 20 MHz, and then all timing (LED data, I²C, gestures)
   is off by 25%.
5. **First boot after flashing:** keep nothing within 40 cm of the sensor,
   ideally in the dark. The sensor calibrates itself and stores the result
   (1 s magenta flash).

Flashing erases the EEPROM, which holds the sensor calibration and the saved size
and color, unless the `EESAVE` fuse is set.

Flash usage is about 3.6 kB, or 3.85 kB with the battery check, out of 4 kB.

### Configuration

All settings are `#define`s.

**[`main.c`](FW/main.c)**

| Define | Default | Meaning |
|---|---|---|
| `DEBUG_DISTANCE` | 0 | 1 = show the raw distance on the LEDs instead of the night light (see [Debug mode](#debug-mode)) |
| `BATTERY_CHECK` | 0 | 1 = battery divider is fitted, enable the battery check |
| `LED_COUNT` | 4 | Number of WS2812 LEDs (~5 bytes RAM each, up to ~40) |
| `TICK_MS` | 10 | Main loop period |
| `ADJUST_NEAR_MM` | 60 | Hand distance for the smallest flame / first color. +1 step per mm up to 255 steps (~32 cm) |
| `TONE_MODE_WAIT_MS` | 2000 | Time to bring the hand back after a double wave |
| `TONE_MODE_END_MS` | 1000 | Color mode ends this long after the hand left |
| `BATTERY_PERIOD_MS` | 60000 | Battery check interval while running |
| `SENSOR_ACTIVE_MS` | 33 | Sensor measurement period while active |
| `SENSOR_IDLE_MS` | 100 | Sensor measurement period while idle (max 255). Longer = less power, more missed flicks |
| `IDLE_AFTER_MS` | 15000 | Light off and nothing happening this long → idle |

**[`gesture.h`](FW/gesture.h)**

| Define | Default | Meaning |
|---|---|---|
| `GESTURE_NEAR_MM` | 600 | Maximum hand distance |
| `GESTURE_DROPOUT` | 2 | Missed readings in a row before a hand counts as gone (bridges glitches near the range limit) |
| `GESTURE_BG_MARGIN_MM` | 80 | A hand must be this much closer than the background |
| `GESTURE_BG_TOL_MM` | 30 | Readings within this count as "not moving" |
| `GESTURE_BG_LEARN_MS` | 10000 | Static this long → becomes background |
| `GESTURE_WAVE_MIN_MS` | 50 | Shorter presence = noise |
| `GESTURE_WAVE_MAX_MS` | 600 | Longer presence = hold. Raise it if your waves are slow |
| `GESTURE_DOUBLE_GAP_MS` | 500 | Maximum pause between the two waves of a double wave |
| `GESTURE_HOLD_MOVE_MM` | 30 | A hold only adjusts after the hand moved this much |
| `GESTURE_HISTORY` | 6 | Readings (~200 ms) rolled back when the hand leaves |

**[`light.h`](FW/light.h)**

| Define | Default | Meaning |
|---|---|---|
| `LIGHT_FADE_ON_MS` / `LIGHT_FADE_OFF_MS` | 800 / 1500 | Fade times |
| `LIGHT_AUTO_OFF_MIN` | 0 | Turn off after this many minutes, 0 = never |
| `LIGHT_DEFAULT_BRIGHTNESS` | 255 | Flame size before anything is saved |
| `LIGHT_MIN_BRIGHTNESS` | 24 | Smallest flame size |
| `LIGHT_DEFAULT_TONE` | 0 | Color before anything is saved (0 = red-orange flame). ~10 gives a more orange flame |
| `LIGHT_WB_G` / `LIGHT_WB_B` | 176 / 240 | White balance: WS2812 green and blue are much stronger than red. Tune until the 4000 K white looks neutral |
| `FLAME_SPREAD` | 110 | How much the flame flickers above its base heat |
| `FLAME_CHANGE` | 40 | How often the flicker changes (chance per tick, of 256) |
| `FLAME_GUST` | 2 | How often the whole flame dips (chance per tick, of 256) |
| `FLAME_SPEED` | 2 | Smoothing; higher = calmer flame |
| `FLAME_HUE_SHIFT` | 24 | How much hotter parts shift along the color strip |
| `LIGHT_WHEEL_END` | 192 | Where the color wheel ends and the white strip starts |

**[`battery.h`](FW/battery.h)**

| Define | Default | Meaning |
|---|---|---|
| `BATTERY_LOW_MV` | 3500 | Warn when turning on |
| `BATTERY_CRITICAL_MV` | 3300 | Refuse to turn on / turn off. Cleared above `BATTERY_LOW_MV` |
| `BATTERY_R_TOP` / `BATTERY_R_BOTTOM` | 1000 / 470 | Divider resistors in kΩ |

### How it works

**Sensor.** The TMF8801 runs the measurement app built into its ROM. No
firmware is uploaded, because the ~11 kB patch doesn't fit in 4 kB.
On the first boot the sensor runs its factory calibration (crosstalk). The
14-byte result is stored in EEPROM and sent to the sensor on every start.
It measures with 900k iterations; each measurement takes ~27 ms.

**Gestures.** Every sensor result goes into `gesture_reading()`, which
decides whether a hand is present. `gesture_update()` runs every 10 ms and
turns presence over time into events: wave, double wave, hold, hold end,
track, track end. While color mode is active, wave and hold detection are off
and the hand position is reported directly ("tracking"). When color mode ends,
a hand that is still there is ignored until it leaves, so color mode never
turns into a size change.

**Light.** The output level fades linearly in perceptual brightness and is
gamma-corrected, so fades look even. Each LED has a "heat" value that
drifts towards random targets. Neighboring LEDs are coupled, so the flicker
travels along the strip, and occasionally the whole flame dips (a gust).
Heat sets both the brightness and a small color shift. The size setting moves
the base heat up and down.

**Power.** While the light is on, the MCU stays awake and the sensor
measures every 33 ms. After `IDLE_AFTER_MS` with the light off, no hand, no
fade and no color mode:
- the sensor switches to `SENSOR_IDLE_MS` and goes to standby between
  measurements
- the MCU goes to power-down and is woken by the sensor's INT pin, which goes
  low on every result (the TMF8801 has no distance-threshold interrupt)
- after each wake, the firmware catches up the missed 10 ms ticks so all timing
  stays correct, then sleeps again unless a hand was seen

Unused pins have their digital inputs disabled, and the battery divider only
draws current during a measurement. A watchdog (~8 s, also running in sleep)
resets the device if the sensor ever stops waking it.

| Approximate current (datasheet values, not measured) | Active | Idle |
|---|---|---|
| TMF8801 | ~27 mA | ~9 mA at 100 ms (~4.5 mA at 200 ms) |
| ATtiny416 | a few mA | µA range plus short wakes |
| 4× WS2812 when dark (plus the flame itself while on) | 2-4 mA | 2-4 mA (hardware) |

**Settings.** The flame size and color are saved to EEPROM when a hold or
color mode ends, and only if they changed. EEPROM layout: page 0 =
calibration + marker, page 1 = size, color + marker.

### Debug mode

Set `DEBUG_DISTANCE 1` to show what the sensor sees, at 10 cm resolution:

- **Color = 40 cm band:** green 0-40, yellow 40-80, red 80-120,
  magenta 120-160, white over 160 cm
- **Number of lit LEDs = 10 cm steps within the band** (1-4). For example,
  3 yellow = 60-70 cm.
- **First LED blue:** the sensor reports no object
- **All cyan:** no new reading for 0.5 s

The display assumes 4 LEDs.

### Sensor range

With the ROM firmware, the TMF8801 here reports "no object" beyond ~64 cm,
even for a white wall in the dark with calibration. The datasheet only
promises its 2.5 m range with the latest ams RAM patch (~11 kB, uploaded
on every boot) and a tuned oscillator. An ATtiny1616 (16 kB) in the same
package fits the patch. The steps are in [`FW/PATCHING.md`](FW/PATCHING.md).

---

## References

- TMF8801 datasheet DS000648 v7-00 (ams OSRAM): register map, commands,
  bootloader protocol, calibration
- [SparkFun TMF8801 Arduino library](https://github.com/sparkfun/SparkFun_TMF8801_Arduino_Library)
  and [DFRobot TMF8x01 library](https://github.com/DFRobot/DFRobot_TMF8x01):
  measurement command bytes, calibration flow, RAM patch data
