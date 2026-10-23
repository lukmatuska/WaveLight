# TMF8801 RAM patch (for ATtiny1616 / 3216)

## Why

Without the patch the sensor runs its old ROM algorithm and reports "no object"
beyond ~64 cm, even on a white wall in the dark with calibration. The datasheet
(DS000648 v7-00, section 7.6) only promises its range (up to 2.5 m) with the
latest ams RAM patch loaded. The patch is ~11 kB and has to be uploaded over I2C
on **every** power-up (EN low erases it), so it does not fit in the ATtiny416
(4 kB flash).

The ATtiny1616 (16 kB) / ATtiny3216 (32 kB) come in the same VQFN-20 package
as the ATtiny416; check the pinout in their datasheets before swapping the chip.

## 1. Switch the MCU

1. Solder the new chip.
2. In the MPLAB project (`.vscode/Wavelight-firmware.mplab.json`), change
   `"device": "ATtiny416"` to `"ATtiny1616"` (or `"ATtiny3216"`). Same
   `ATtiny_DFP` pack.
3. Set the optimization level to `-O2` (project properties → XC8 compiler →
   Optimizations).
4. Set the fuses again on the new chip: **OSCCFG = 16 MHz** (factory default is 20 MHz).

No code changes are needed for the switch itself: the TWI, TCB, NVMCTRL and
EEPROM registers are the same. Build and check the night light works exactly
as before *before* adding the patch.

## 2. Get the patch data

Source: DFRobot's TMF8801 library (MIT licensed),
`https://github.com/DFRobot/DFRobot_TMF8x01`, file `src/drv/TMF8801_2.h`.

It contains `DFRobot_TMF8801_initBuf[]`: a list of records `len, data[len]`,
terminated by a single `0`. 677 records of 16 bytes + 1 of 13 bytes =
**10845 bytes of payload, 11524 bytes total** including length bytes and
terminator (as of 2026-10). The data is
encrypted, so it can't be compressed or inspected.

ams may have a newer patch version. The datasheet says to "contact ams to
identify the latest patch version".

Convert it to a plain C header (drops Arduino's `PROGMEM`; on tinyAVR `const`
data stays in flash and is readable directly):

```sh
python3 - TMF8801_2.h > tmf8801_patch.h <<'EOF'
import re, sys
s = open(sys.argv[1]).read()
body = s[s.index('{') + 1 : s.index('}')]
b = re.findall(r'0x[0-9A-Fa-f]{2}|\b0\b', body)
print('// TMF8801 RAM patch from DFRobot_TMF8x01 (src/drv/TMF8801_2.h), MIT licensed')
print('// Records: len, data[len] ... terminated by 0')
print('static const uint8_t tmf8801_patch[] = {')
for i in range(0, len(b), 17):
    print('    ' + ', '.join(x if x != '0' else '0x00' for x in b[i:i + 17]) + ',')
print('};')
EOF
```

Check that the last value in the array is `0x00`. That is the terminator.

## 3. Bootloader protocol

After power-on (`ENABLE = 0x01`, wait for `ENABLE == 0x41`) the sensor is in its
bootloader (`APPID` 0x00 reads `0x80`). Bootloader commands are written to
register `0x08`:

```
0x08: cmd, len, data[len], checksum      checksum = ~(cmd + len + data...) & 0xFF
```

After each command, poll 3 bytes from `0x08` until they read `00 00 FF`
(status READY, no data, checksum of nothing).

| Step | cmd | data | Meaning |
|---|---|---|---|
| 1 | `0x14` | `0x29` | DOWNLOAD_INIT |
| 2 | `0x43` | `0x00 0x00` | set RAM address to 0 |
| 3 | `0x41` | one patch record (16 bytes) | write RAM, repeat for every record |
| 4 | `0x11` | none | RAMREMAP_RESET: run the patch (no status reply, the chip resets) |

After step 4, wait for `ENABLE == 0x41` (CPU ready) and then `APPID == 0xC0`.
**Don't** write `APPREQID = 0xC0`: the patched app starts by itself.

The datasheet (DS000648 section 8.10, "Bootloader Commands") confirms the
checksum on **every** command, e.g. RAMREMAP_RESET is `11 00 EE`. DFRobot sends
steps 1, 2 and 4 without it, and its status check has a bug
(`if (value = 0xFF0000)`) that makes it always pass, so don't copy their bytes.

W_RAM accepts up to 128 data bytes per command. Sending several 16-byte records
per command would speed up the upload. The sketch below keeps one record per
command because it's simpler (it needs a bigger buffer otherwise).

## 4. Code sketch for `tmf8801.h`

Written against the existing helpers (`twi_write_reg`, `twi_read_reg`,
`tmf8801_wait_for`, `delay_ms`). Untested.

```c
#include "tmf8801_patch.h"

#define TMF8801_REG_BL_CMD   0x08
#define TMF8801_APP_BOOT     0x80
#define TMF8801_BL_INIT      0x14
#define TMF8801_BL_ADDR_RAM  0x43
#define TMF8801_BL_W_RAM     0x41
#define TMF8801_BL_RESET     0x11

/**
 * Send one bootloader command
 * @param wait 1 = wait for the READY status (00 00 FF)
 */
static uint8_t tmf8801_bl_cmd(uint8_t cmd, const uint8_t *data, uint8_t len, uint8_t wait) {
    uint8_t buf[2 + 16 + 1];
    uint8_t sum = cmd + len;

    buf[0] = cmd;
    buf[1] = len;
    for (uint8_t i = 0; i < len; i++) {
        buf[2 + i] = data[i];
        sum += data[i];
    }
    buf[2 + len] = ~sum;

    if (twi_write_reg(TMF8801_ADDR, TMF8801_REG_BL_CMD, buf, len + 3)) return 1;
    if (!wait) return 0;

    for (uint8_t t = 0; t < 100; t++) {
        uint8_t st[3];
        twi_read_reg(TMF8801_ADDR, TMF8801_REG_BL_CMD, st, 3);
        if (st[0] == 0x00 && st[1] == 0x00 && st[2] == 0xFF) return 0;
        delay_ms(1);
    }
    return 1;
}

/**
 * Upload the RAM patch, the chip must be in the bootloader
 * @return 0 when the patched app is running
 */
static uint8_t tmf8801_patch_upload(void) {
    static const uint8_t init[] = {0x29};
    static const uint8_t addr[] = {0x00, 0x00};

    if (tmf8801_bl_cmd(TMF8801_BL_INIT, init, 1, 1)) return 1;
    if (tmf8801_bl_cmd(TMF8801_BL_ADDR_RAM, addr, 2, 1)) return 1;

    for (const uint8_t *p = tmf8801_patch; *p; p += *p + 1) {
        if (tmf8801_bl_cmd(TMF8801_BL_W_RAM, p + 1, *p, 1)) return 1;
    }

    tmf8801_bl_cmd(TMF8801_BL_RESET, 0, 0, 0);
    if (tmf8801_wait_for(TMF8801_REG_ENABLE, 0x41, 100)) return 1;
    return tmf8801_wait_for(TMF8801_REG_APPID, TMF8801_APP_MEAS, 100);
}
```

In `tmf8801_init()`, replace

```c
    tmf8801_write(TMF8801_REG_APPREQID, TMF8801_APP_MEAS);
    return tmf8801_wait_for(TMF8801_REG_APPID, TMF8801_APP_MEAS, 100);
```

with

```c
    if (tmf8801_read(TMF8801_REG_APPID) == TMF8801_APP_BOOT) return tmf8801_patch_upload();
    return 0;
```

Everything after that (calibration, `tmf8801_start()`, result polling) stays
the same.

## 5. Test

1. Turn on `DEBUG_DISTANCE 1` in `main.c`.
2. **Reflash** so that the EEPROM is erased and the sensor recalibrates with the
   patched algorithm. Calibration data from the ROM app may not suit the patch.
   During the first boot, nothing should be within 40 cm of the sensor, and keep the room dark.
   Wait for the 1 s magenta flash.
3. Point at a wall. Expected: readings past 64 cm (red / magenta / white bands)
   instead of the first LED turning blue.
4. If it blinks red: the upload failed. Check the status bytes after each command (section 3).
   The firmware has an ~8 s watchdog (main.c), so the whole boot including the upload must stay below that.
   At 50 kHz I2C the upload takes ~2.5 s, so a startup delay is normal.
   You can go back to 100 kHz in `twi.h` if the bus is reliable with real pull-ups.

Size check (this sketch built for ATtiny1616 on 2026-10-22): 14103 B at `-O0`,
13281 B at `-O2`, out of 16384.

## Later: oscillator tuning

The datasheet also says full range needs the sensor's oscillator tuned to
4.7 MHz "using the ams reference code". The datasheet doesn't document the
procedure. The `SYS_CLOCK` registers (0x24-0x27, read as one block from 0x1D to 0x27)
can be compared against the MCU clock, but the actual trim has to come from the
ams reference driver. Do this only if the patched range is still short.
