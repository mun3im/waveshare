# Waveshare ESP32-S3-Touch-LCD-7 — Diagnostics Datasheet

Compiled from live USB/serial/flash diagnostics on 2026-08-07.
Board model: **ESP32-S3 7" Touch Display, 800×480, Xtensa LX7 dual-core, 8MB Flash (nominal), WiFi + BT5, GUI/LVGL/HMI**.

Status as of last check: **display is dark** (factory firmware present but not producing visible output).

---

## 1. Host connection

| Item | Value |
|---|---|
| Interface | Native USB (USB-Serial/JTAG, not an external USB-UART bridge chip) |
| macOS device node | `/dev/cu.usbmodem101` (also `/dev/tty.usbmodem101`) |
| USB registry name | `USB JTAG/serial debug unit` |
| First seen enumerated | 2026-08-07 08:18 |
| Driver needed | None — native CDC, no CH340/CP2102 driver required |

## 2. Chip identification (via esptool)

| Item | Value |
|---|---|
| Chip type | ESP32-S3 (QFN56), silicon revision v0.2 |
| Cores | Dual-core Xtensa LX7 + LP core |
| Clock | 240 MHz max, 40 MHz crystal |
| Wireless | Wi-Fi, Bluetooth 5 (LE) |
| PSRAM | Embedded 8MB (AP_3v3) |
| MAC address | `d8:3b:da:xx:xx:xx` (device-specific suffix redacted; `d8:3b:da` is the Espressif OUI) |
| USB mode | USB-Serial/JTAG |
| esptool version used | v5.3.1 |

## 3. Flash memory

| Item | Value |
|---|---|
| Manufacturer ID | 0x46 |
| Device ID | 0x4018 |
| Detected size | **16MB** (note: listing title says 8MB — actual chip is larger) |
| Flash type (eFuse) | Quad (4 data lines, QIO) |
| Flash voltage (eFuse) | 3.3V |

## 4. Boot / reset behavior

ROM bootloader response on reset (`USB_UART_CHIP_RESET` via DTR/RTS toggle):

```
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0x8 (SPI_FAST_FLASH_BOOT)
Saved PC:0x40378d96
SPIWP:0xee
mode:DIO, clock div:1
load:0x3fce2820,len:0x118c
load:0x403c8700,len:0x4
load:0x403c8704,len:0xc20
load:0x403cb700,len:0x30e0
entry 0x403c88b8
ESP-ROM:esp32s3-20210327
Build:Mar 27 2021
rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))
Saved PC:0x40378d96
waiting for download
```

ROM bootloader responds correctly and cleanly enters download mode on request — chip and USB-JTAG/serial path are fully functional.

## 5. Partition table (read from flash offset `0x8000`)

Valid partition table (magic `0xAA50` present on every entry).

| Name | Type/Subtype | Offset | Size |
|---|---|---|---|
| `nvs` | data / nvs | 0x9000 | 0x5000 (20 KB) |
| `otadata` | data / ota | 0xe000 | 0x2000 (8 KB) |
| `app0` | app / ota_0 | 0x10000 | 0x140000 (1.25 MB) |
| `app1` | app / ota_1 | 0x150000 | 0x140000 (1.25 MB) |
| `spiffs` | data / spiffs | 0x290000 | 0x160000 (1.375 MB) |
| `coredump` | data / coredump | 0x3f0000 | 0x10000 (64 KB) |

Dual-OTA layout (app0/app1), not a bare single-app Arduino sketch — this is a production-style factory image.

## 6. Firmware currently flashed (app0, read and inspected)

| Item | Value |
|---|---|
| App image magic | `0xE9` (valid ESP image), version 6 — bootloader magic also `0xE9`, valid |
| Build framework | Arduino-ESP32 core on top of ESP-IDF |
| Board FQBN (embedded string) | `espressif:esp32:waveshare_esp32_s3_touch_lcd_7` |
| Board identifier string | `WAVESHARE_ESP32_S3_TOUCH_LCD_7` |
| Build options (embedded) | `UploadSpeed=921600, USBMode=hwcdc, CDCOnBoot=default, MSCOnBoot=default, DFUOnBoot=default, UploadMode=default, CPUFreq=240, FlashMode=qio, FlashSize=8M, PartitionScheme=default, DebugLevel=none, PSRAM=enabled, LoopCore=1, EventsCore=1, EraseFlash=none` |
| Touch controller driver compiled in | **GT911** capacitive touch (`esp_lcd_touch_gt911_*`, `esp_lcd_touch_new_i2c_gt911`) |
| Conclusion | This is Waveshare's **factory demo firmware** for this exact board, not user-flashed code |

No LVGL library debug strings were matched in this scan pass (may be stripped, or present only in symbol-free form) — presence of an LVGL demo is likely given the "GUI LVGL HMI" product description, but not directly confirmed by string search.

## 7. Observed problem (RESOLVED — see §9)

- Board powers up, enumerates over USB, and responds normally to esptool/serial commands.
- **Display panel is dark** — no visible output despite factory firmware being present and the chip being alive.
- Root cause found and fixed: see §9 below. Was a PSRAM build-config issue, not a hardware fault.

## 8. Tooling used

- `esptool` v5.3.1 (installed via `pip3 install --user esptool`, binaries in `~/.local/bin`)
- Reads performed: `chip_id`, `flash_id`, `read_flash` at `0x0` (bootloader), `0x8000` (partition table), `0x10000`–`0x150000` (full app0 partition)
- Serial boot log captured via `pyserial` with manual DTR/RTS toggle to force `USB_UART_CHIP_RESET`

---

## 9. Dark-display root cause & fix (resolved 2026-08-07)

**Symptom**: display stayed dark under factory firmware; flashing a custom LVGL "bouncing ball" sketch also produced a dark screen, and additionally caused the native USB-JTAG/serial port (`/dev/cu.usbmodem101`) to vanish entirely after reset (40+ seconds, no re-enumeration).

**Investigation path**:
1. Board has a **second USB-C port** ("bottom" port) wired to a separate **CH34x USB-UART bridge chip**, independent of the ESP32-S3's native USB. It enumerated as `/dev/cu.wchusbserial<id>` and stayed alive even while the native USB port was hung — this made it possible to read logs when the primary port was unresponsive.
2. Serial log via the CH34x port revealed the real failure chain:
   ```
   E lcd_panel.rgb: lcd_rgb_panel_alloc_frame_buffers(163): no mem for frame buffer
   E lcd_panel.rgb: esp_lcd_new_rgb_panel(344): alloc frame buffers failed
   [E][Panel][esp_panel_lcd_st7262.cpp:0079](init): Create refresh panel failed [ESP_ERR_NO_MEM]
   [E][Panel][esp_panel_board.cpp:0315](begin): LCD device begin failed
   [E][LvPort] LCD device is not initialized
   [E][LvPort] LVGL mutex is not initialized
   Guru Meditation Error: Core 1 panic'ed (LoadProhibited)
   ```
3. **Root cause**: the ST7262 RGB LCD panel needs a large frame buffer — 800×480×3 bytes (RGB888) × 2 (double-buffered) ≈ **2.3MB** — which cannot fit in the ESP32-S3's internal SRAM (~390KB usable) and must come from PSRAM. The Arduino board definition's `PSRAM` option **defaults to `disabled`**. With PSRAM off, the allocation failed → LCD init failed → LVGL init failed → sketch dereferenced an uninitialized LVGL mutex → kernel panic. The panic mid-way through USB stack bring-up is why the native USB-JTAG port stopped enumerating after that flash.
4. The panel's backlight/reset lines are **not plain GPIOs** — they're behind a **CH422G I2C IO expander** (I2C addr `0x20`, SCL=GPIO9, SDA=GPIO8): `LCD_BL`=expander pin 2, `LCD_RST`=expander pin 3, `TP_RST`=expander pin 1. This was correctly handled by the `ESP32_Display_Panel` board driver once it could actually complete init — it was never the blocker itself, but is worth recording since it explains why a naive GPIO-only backlight sketch would also fail.

**Fix applied**: compiled with the Arduino board option **`PSRAM=enabled`** (FQBN: `espressif:esp32:waveshare_esp32_s3_touch_lcd_7:PSRAM=enabled`). Reflashed via the CH34x bridge port to avoid the wedged native-USB port. Board booted clean:
```
[I][Panel] Board begin success
[I][Panel][esp_lcd_touch_gt911.c] TouchPad_ID:0x39,0x31,0x31   (GT911 = "911", touch controller detected OK)
[I][LvPort] Initializing LVGL display driver
Setup done
```
**Result**: display lit up, bouncing ball animation confirmed working on-screen (2026-08-07).

**Toolchain/library notes for future builds on this board**:
- Environment already had `arduino-cli`, `espressif:esp32` core 3.2.0, and the `ESP32_Display_Panel` / `ESP32_IO_Expander` / `esp-lib-utils` libraries pre-installed and matched to this board.
- `ESP32_Display_Panel`'s bundled LVGL port example (`examples/arduino/gui/lvgl_v8/simple_port`) is written for **LVGL v8's** driver API (`lv_disp_drv_t`, `lv_indev_drv_t`, etc.). The environment's globally-installed `lvgl` library was v9.5.0, which removed those APIs — had to downgrade the shared `lvgl` library to **v8.4.0** (`arduino-cli lib install lvgl@8.4.0`) to compile. This is a machine-wide change; any other sketch relying on LVGL v9 APIs would need `lvgl` reinstalled to 9.x before building.
- To select this board in `ESP32_Display_Panel`, set `ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED` to `1` and uncomment `#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7` in `esp_panel_board_supported_conf.h` (copied per-sketch from the library's example folder, not edited in the shared library install).
- **Always build with `PSRAM=enabled`** for any sketch using the RGB LCD panel on this board — this is the single most important non-obvious setting.
- Sketch project location: `~/Documents/Arduino/BouncingBall/`

## 10. Factory firmware restored (2026-08-07)

After confirming the bouncing ball demo worked, the original factory firmware was restored to `app0`:

- The full `app0` partition (1,310,720 bytes) had been read out via `esptool read_flash 0x10000 0x140000` during initial diagnostics (§6), **before** any custom firmware was flashed — saved at `app0_full.bin`.
- Note: `app1` (the second OTA slot) was checked and found to **not** contain a valid factory backup (no `0xE9` image magic, non-blank but not a bootable app) — so restoring relied on the saved `app0_full.bin` dump, not `app1`.
- Restored via `esptool write_flash 0x10000 app0_full.bin`, hash-verified by esptool after write.
- Post-restore boot log confirmed clean POWERON boot with no crashes, repeatedly printing `Hello World!` — matching the string found embedded in the original factory image during the initial string-search diagnostics (§6). This is the byte-identical original factory image.
- Flashed via the CH34x UART bridge port (`/dev/cu.wchusbserial*`, bottom USB-C port), same as the bouncing ball build.

Board is back to its as-received factory state. The `BouncingBall` sketch remains available at `~/Documents/Arduino/BouncingBall/` to reflash anytime (remember: compile with `PSRAM=enabled`).

## 11. Factory image re-examined: not a real GUI demo (2026-08-07)

After restoring `app0`, the display was still dark. Fresh serial logs from the "factory" image showed it never touches the LCD/touch/expander at all -- it only does `Serial.begin(115200)` and repeatedly prints `Hello World!`. This matches the `Working_LCD7_Serialtest.ino` sketch already present in `~/Documents/Arduino/Waveshare/`, i.e. this board's `app0` likely held a minimal serial/QC-test image (probably from a previous owner or reseller), not Waveshare's actual LVGL demo. There was nothing genuine to preserve, so no further attempt was made to recover it.

Investigated Waveshare's official GitHub org (`waveshareteam`) for the plain (non-B/C) `ESP32-S3-Touch-LCD-7` -- no matching repo exists there; their wiki page also blocks automated fetches. Found a third-party repo, `paulhamsh/Waveshare-ESP32-S3-LCD-7-LVGL`, with LVGL 9 demos for this exact board (`Waveshare_7_LVGL_9`: a slider + button demo via `Arduino_GFX_Library` + `TAMC_GT911`). Inspection found it never initializes the CH422G IO expander (no LCD reset pulse, no explicit backlight enable) -- a likely dark-screen risk on this hardware. Patched in the expander bring-up sequence, but this surfaced a deeper incompatibility: `TAMC_GT911` uses Arduino's `Wire` I2C driver while `ESP32_IO_Expander`'s `CH422G` class owns its own native ESP-IDF `i2c_master` bus on the same physical pins -- two different I2C driver stacks contending for the same bus. Decided this was too much unverified third-party surgery for the payoff and abandoned that path in favor of the already-proven `ESP32_Display_Panel` + LVGL stack.

## 12. Bouncing ball restored, then extended with WiFi + NTP clock (2026-08-07)

Reflashed the known-working `BouncingBall` sketch (§9) -- confirmed clean boot and working display again.

**Feature added**: on boot, connect to WiFi (credentials from `wifi_credentials.h`, copied into the sketch folder from `~/wifi_credentials.h`), sync time via NTP (`pool.ntp.org` / `time.google.com`, GMT+8 / UTC+8, no DST), then show the live `HH:MM:SS` clock as a label centered inside the bouncing ball.

**Crash hit and fixed**: with WiFi connect code called *after* board/LCD/LVGL init (natural ordering), the board crash-looped deterministically every boot:
```
Connecting to WiFi...
Guru Meditation Error: Core 1 panic'ed (Cache disabled but cached memory region accessed)
EXCCAUSE: 0x00000007 (LoadStoreError)
```
Always at the same point (`WiFi.begin()`), every single boot. Root cause: a known interaction on ESP32-S3 between the RGB LCD driver's bounce-buffer refresh (which reads cached PSRAM from an ISR) and WiFi's `WiFi.begin()` (which briefly disables the flash cache for its own flash/NVS access) -- documented upstream as [esp-arduino-libs/ESP32_Display_Panel#198](https://github.com/esp-arduino-libs/ESP32_Display_Panel/issues/198). The library's own fix note says to enable "RGB LCD Bounce Buffer + XIP on PSRAM," but XIP-from-PSRAM is an ESP-IDF Kconfig option (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) not exposed in the stock Arduino IDE board menu for this core.

**Workaround applied**: reordered `setup()` so WiFi connects and NTP syncs to completion **before** the `Board`/LCD/LVGL are initialized at all -- i.e. no RGB LCD bounce-buffer ISR exists yet while WiFi's cache-disabling calls run, so the race never occurs. This is a pure code reorder, no new board/library settings. Verified with a fresh boot log:
```
Connecting to WiFi (before LCD init, to avoid RGB+WiFi cache race)...
.....
WiFi connected, IP: 192.168.0.160
Waiting for NTP time sync...
Time synced: 2026-08-07 09:16:16 (GMT+8)
Initializing board
...
Board begin success
...
Setup done
```
No crash. Confirmed visually: ball bounces around the screen with a live clock updating inside it.

**Flash usage note**: adding WiFi pushed the sketch to **1,248,592 bytes / 95% of the 1.25MB `app0` partition**. Very little headroom left (~62KB) for further features in this sketch without either trimming code or repartitioning flash (the board's 16MB flash is mostly unused across the OTA/spiffs/coredump partitions, so a custom partition table with a larger single-app slot would free up a lot of room if needed).

**Caveat**: the reorder is a practical workaround, not a root-cause fix of the underlying cache race. If a future version of this sketch needs to call WiFi/flash-touching APIs (e.g. re-connect after a drop, OTA update, `Preferences`/NVS writes) *after* the LCD/LVGL are already running, the same crash risk returns. At that point, either the RGB LCD bounce buffer would need XIP-from-PSRAM enabled via a custom `sdkconfig`/ESP-IDF build (not available through stock Arduino IDE board options), or WiFi/flash operations would need to be scheduled to briefly pause the LVGL refresh timer around them.

Sketch location: `~/Documents/Arduino/BouncingBall/` (`BouncingBall.ino`, `wifi_credentials.h`).

## 13. microSD (TF) card slot — physical location (2026-08-07)

Board advertises an onboard TF/microSD card slot (confirmed both by Waveshare's docs page, which lists it as labeled item #4 in the board diagram, and by the `SD_CS` pin already present in the CH422G IO-expander pin map found during earlier firmware analysis — see `Waveshare_ST7262_LVGL.h`: `TP_RST=1, LCD_BL=2, LCD_RST=3, SD_CS=4, USB_SEL=5`).

It is **not visible or accessible from the outside** of the assembled board — it's physically **tucked underneath the LCD panel module**. To insert/remove a card, the LCD needs to be unscrewed from the mainboard (a few screws), or a card can be worked in/out with tweezers through the gap without full disassembly. Not a defect — just a non-obvious physical design choice, likely because the SD interface shares the same IO-expander/connector region as the display.

## 14. ClockDisplay v1 — large clock, touch to toggle digital/analog (2026-08-07)

New project (separate from `BouncingBall`) at `~/Documents/Arduino/ClockDisplay/`. Full-screen clock; tapping anywhere toggles between a large digital readout and an analog clock face (LVGL `lv_meter` widget with hour/minute/second needles). Same WiFi-before-LCD boot order and board bring-up as `BouncingBall` (see §12).

**Flash ran out of room**: this build (WiFi + LVGL `lv_meter` widget + extra fonts) came to ~1.35MB, exceeding the default 1.25MB `app0` OTA partition (102% - wouldn't fit). Fixed by switching partition scheme rather than trimming features, since v2 (prayer times) and v3 (photo frame) will need more room, not less:
```
FQBN options: FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled
```
This board's flash is 16MB (see §3) but Arduino's default scheme only assumes 8MB with a 1.25MB app slot -- `app3M_fat9M_16MB` gives **3MB for the app** (currently at ~43% usage, real headroom) **+ 9.9MB FATFS** for future data/photo storage. Trade-off: no more dual-OTA (single app partition), which is fine for a hobby project reflashed over USB. **Use these FQBN options for all `ClockDisplay` (and later) builds going forward.**

**Bugs hit and fixed while building v1**:
1. *Tap only worked one direction (digital -> analog, not back)*: the `lv_meter` widget is clickable by default in LVGL 8 and was consuming the tap event before it could bubble up to the screen-level click handler. Fix: `lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE)` on the meter object so taps pass through to the screen.
2. *Digital font too small*: LVGL's largest built-in font is `lv_font_montserrat_48`, which alone only fills a fraction of the 800px width. Fixed by applying `lv_obj_set_style_transform_zoom(label, 700, 0)` (256 = 1x) to scale the rendered label ~2.7x -- a practical way to get a "large clock" look without generating/shipping a custom huge bitmap font.

**v1 feature set (confirmed working)**: WiFi + NTP boot (GMT+8), full-screen digital clock (large font, date subtitle), full-screen analog clock (60-tick scale, 5-tick hour marks, hour/minute/second needles), tap-anywhere toggle between the two, WiFi/NTP failure shown as a status banner.

**Planned roadmap** (per user, not yet built):
- v2: Muslim daily prayer times (5-6 times/day), selectable/viewable via touch.
- v3: photo frame mode, selectable via touch.
- Discussed and deferred: storage format for 365 rows x 6 prayer times/day. Decided against SQLite on-device (real SQL engine is unnecessary overhead for a fixed 365x6 read-only lookup table on a microcontroller) in favor of a flat file (binary or CSV) on the newly-provisioned FATFS partition, indexed directly by day-of-year -- to be designed in detail when v2 work starts.

**Follow-up bugs found and fixed after initial v1 testing** (digital clock centering):
1. *Time label rendered off-center, date overlapped it*: `lv_obj_set_style_transform_zoom()` only scales what's *drawn* -- the object's layout box (used for positioning) stays at the pre-zoom size, and LVGL 8's default zoom pivot is the object's top-left corner, not its center. Fixed by explicitly setting `transform_pivot_x/y` to the label's own center and computing screen position from the unzoomed box size directly (no `lv_obj_align`).
2. *Time label still drifted right/down after the above fix*: turned out to be the same pivot issue restated -- confirmed fixed once the explicit center pivot was set correctly.
3. *Time label statically anchored top-left as digits changed (e.g. "1" vs "8" have different glyph widths in Montserrat)*: the label was measured and centered **once** at startup using sample text ("88:88:88"), then real text was swapped in afterward without ever re-measuring. Since digit glyphs aren't fixed-width, the label's true rendered width -- and therefore where "centered" actually is -- shifts every second. Fixed with a `recenter_time_label()` helper that re-measures the label's current width/height and repositions/re-pivots it on **every** text update (called from `update_digital_display()`, not just once at creation). The date label's vertical position is computed once (font height is constant regardless of which digits show, only width varies) and doesn't need to move on every tick.

**Follow-up bug found and fixed after that** (analog clock face): seconds hand didn't align with the tick marks, worst in the bottom half of the dial. Root cause found in LVGL 8's `lv_meter.c`: the tick-mark angle generator spaces N ticks across **N-1** gaps (`i * angle_range / (tick_cnt - 1)`), while a needle's angle is `lv_map()`'d across the **full** `(max - min)` value range. With `tick_cnt=60` over a `0..60` value range on a 360 degree scale, those two don't agree (59 gaps vs 60 units) -- the per-step error is tiny but accumulates going around the circle, and is least visible near the top (shared 0/60 seam) and most visible by the bottom. Fixed by setting `tick_cnt = 61` (`61 - 1 == 60`, matching the value range exactly); ticks 0 and 60 now correctly land on the same angle at the top.

Sketch location: `~/Documents/Arduino/ClockDisplay/` (`ClockDisplay.ino` + same board/LVGL config files as `BouncingBall`, plus `wifi_credentials.h`).

## 15. ClockDisplay v2 — prayer times screen (2026-08-07)

Added a third mode to the tap cycle: digital -> analog -> **prayer times** -> digital. Shows zone JHR02 (Johor Bahru) daily prayer times: Imsak, Subuh, Syuruk, Zuhur, Asar, Maghrib, Isyak, with the next upcoming prayer today highlighted (`<-- next`).

**Data pipeline**: prayer times sourced from http://myrakan.com/waktusolat as printable PDFs (site itself unreachable from the dev sandbox -- user fetched PDFs manually), for August and September 2026 (61 days total; user could not access other months from the source at this time). Converted HH:MM times to minutes-past-midnight and stored as a C 2D array:
- Source of truth: `~/Touchscreen/prayer_times/JHR02_2026-08.csv`, `JHR02_2026-09.csv` (human-readable, one row per day).
- Converter: `~/Touchscreen/prayer_times/csv_to_c_array.py` -- regenerates the embedded table from whatever `JHR02_2026-*.csv` files are present; re-run it whenever more months are added.
- Generated header: `prayer_times_data.h` (copied into the sketch folder) -- `PRAYER_MINUTES[61][7]` (int16_t minutes-past-midnight) plus `PRAYER_DOY[61]` mapping each row to its actual day-of-year (1-366). Firmware looks up "today" by day-of-year via `PRAYER_DOY`, not by row index, so it correctly reports "no data" for any day outside the loaded months rather than assuming a full year is present or silently showing the wrong day.
- Footprint: tiny -- 61x7 `int16_t` is under 1KB, embedded directly in flash (no need for the FATFS partition for this).

**UI implementation notes**:
- Mode state changed from a `bool analog_mode` to a `DisplayMode` enum (`MODE_DIGITAL`/`MODE_ANALOG`/`MODE_PRAYER`/`MODE_COUNT` sentinel) so the tap handler just cycles `(mode + 1) % MODE_COUNT` -- adding a future v3 mode (photo frame) is a one-line change to the enum.
- Prayer screen has two states depending on whether today's day-of-year is found in `PRAYER_DOY`: the times list, or a "No prayer time data for today" fallback label (toggled via `LV_OBJ_FLAG_HIDDEN`) -- exercised automatically for any date outside Aug 1 - Sep 30 2026.

Sketch location unchanged: `~/Documents/Arduino/ClockDisplay/` (now also includes `prayer_times_data.h`).

## 16. PrayerTimes forked into its own sketch + dashboard layout redesign (2026-08-07)

`ClockDisplay` was split into two separate projects:
- **`ClockDisplay`**: reverted to clock-only (digital <-> analog toggle), matching its original v1 scope. `prayer_times_data.h` and all prayer-related code removed.
- **`PrayerTimes`** (new, `~/Documents/Arduino/PrayerTimes/`): a fork of the v2 `ClockDisplay` (with prayer times), now developed independently as the dedicated prayer-times app.

**Mode order changed** so prayer times is the default/home screen: tap cycle is now **Prayer -> Analog -> Digital -> Prayer** (was Digital -> Analog -> Prayer -> Digital). Just an enum reorder (`DisplayMode` values + `current_mode` initial value) -- the existing `(current_mode + 1) % MODE_COUNT` tap-cycle logic needed no other changes.

**Screen 1 (prayer/home) redesigned into a 3-block dashboard**, replacing the original simple title+date+stacked-list layout:
- **Block 1**: full-width title bar, `"Waktu Solat - Johor Bahru"` (dropped the `"(JHR02)"` suffix).
- **Block 2** (left half): a second, independent analog clock meter (`home_meter` + its own needle set) sized for the panel, plus the date, plus a small digital `HH:MM:SS` readout.
- **Block 3** (right half): the 7 prayer rows, each now **two separate label objects** (name + time) instead of one shared multi-line label -- needed so the "next prayer" row's color can be set independently per-row.

**Refactor**: extracted the meter tick/needle setup (including the tick-count-61 fix from #14) into a shared `build_clock_meter(parent, diam, &hour, &min, &sec)` helper, used by both the full-screen analog mode (screen 2) and the new home-screen meter (block 2) -- avoids duplicating that logic (and its explanatory comment) a second time.

**Two follow-up layout bugs found and fixed after initial testing**:
1. *Font choice mixed up*: first pass put the monospace font on the small home-screen digital time readout (block 2) and left Montserrat (proportional) on the prayer list (block 3) with `%-8s` string padding for alignment -- but proportional fonts don't produce aligned columns from padding alone, so the 7 rows didn't line up. Enabled `LV_FONT_UNSCII_16` in `lv_conf.h` (was present but disabled) as the monospace font.
2. *Mono font applied to the wrong part of each row*: putting the whole `"Name  HH:MM"` string in monospace made prayer names ("Imsak", "Maghrib", etc.) look visually ugly/cramped. Fixed by splitting each row into **two labels**: prayer name in `lv_font_montserrat_28` (proportional, reads naturally) positioned at a fixed left x, and time in `lv_font_unscii_16` (zoomed ~1.56x) positioned at a second fixed x (`TIME_COL_X = 260`) -- so times align in a clean column across all 7 rows regardless of each name's rendered width, while names still look like normal text.

**"Next prayer" highlight**: changed from a trailing `" <-- next"` text marker to **yellow text color** (`lv_palette_main(LV_PALETTE_YELLOW)`) on both the name and time label of that row; reset to white every tick since which row is "next" can change as time passes.

Compiles at 44% flash usage (`~/Documents/Arduino/PrayerTimes/`, same FQBN/build options as `ClockDisplay`: `FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled`). Confirmed on-device: boots directly into the new prayer dashboard, names/times both aligned and readable, next prayer shown in yellow, tap cycle confirmed Prayer -> Analog -> Digital -> Prayer.

## 17. PrayerTimes follow-up tweaks (2026-08-07)

Three small adjustments after initial dashboard testing:
- **Time column margin**: shifted `TIME_COL_X` ~38px (~3 mono char-widths) further left, giving the prayer time column more breathing room from block 3's right edge.
- **12-hour time format**: prayer list times changed from 24h `HH:MM` to 12-hour with no leading zero and no AM/PM suffix (e.g. `5:44` not `05:44`, AM/PM omitted as implied-by-context for a prayer-times display). Computed manually (`hour24 % 12`, mapping 0 -> 12) rather than via `strftime`'s `%I`, since that still zero-pads.
- **Dropped monospace font entirely**: after the time column read acceptably aligned even with a proportional font (given the 12-hour reformat made times shorter/more uniform), switched both the prayer list's time labels *and* the block 2 home-screen digital time readout from `lv_font_unscii_16` back to `lv_font_montserrat_28`, matching the rest of the UI. `LV_FONT_UNSCII_16` is left enabled in `lv_conf.h` (harmless, tiny footprint) even though nothing references it now.

**Prayer-time audio alert -- deferred**: user asked for 3 beeps when a prayer time is reached. No speaker/buzzer/I2S hardware is referenced anywhere in this board's `ESP32_Display_Panel` config or in this datasheet's diagnostics -- the Waveshare ESP32-S3-Touch-LCD-7 appears to have no onboard audio output. Confirmed with user to skip this feature for now rather than build against unconfirmed hardware; revisit if an external buzzer/speaker is wired to a GPIO, or a visual on-screen alert is wanted instead.

## 18. Hide Imsak row from UI; data-layout question (2026-08-07)

**Imsak hidden from the on-screen list** (Subuh through Isyak still shown, 6 rows instead of 7). The underlying data is untouched -- `prayer_times_data.h`'s `PRAYER_MINUTES[][7]` still has all 7 columns, including Imsak. Implemented via a new `VISIBLE_PRAYER_COLS[]` array (`{PRAYER_SUBUH, PRAYER_SYURUK, PRAYER_ZUHUR, PRAYER_ASAR, PRAYER_MAGHRIB, PRAYER_ISYAK}`) that both the row layout and the update loop iterate over instead of all `PRAYER_TABLE_COLS`. The "next prayer" yellow-highlight search now also only considers visible columns, so Imsak (even though its data is still read) can never be silently highlighted as "next" while being invisible on screen. Row height/count adjusted so the 6 visible rows fill block 3 evenly.

**Data layout question**: asked whether bundling each day's row into a `struct { int16_t doy; int16_t times[7]; }` (array-of-structs) would be more efficient than the current parallel-arrays layout (`PRAYER_DOY[61]` + `PRAYER_MINUTES[61][7]`, struct-of-arrays). Evaluated and **not implemented** -- for this data size (61 rows, ~976 bytes either way) and access pattern (linear scan to find `doy`, then read 7 contiguous values), memory and speed are identical between the two layouts; the struct form would only be marginally more readable. Left the array layout as-is rather than restructure without a real efficiency gain -- consistent with only implementing the change if it's actually more efficient, as asked.

## 19. Buzzer GPIO research: I2C header selected, PCF8574 planned (2026-08-07)

Researched GPIO options for a prayer-time audio alert (3 beeps), given the board has no onboard speaker/buzzer/I2S hardware (confirmed via `docs.waveshare.com/ESP32-S3-Touch-LCD-7` -- 21-item onboard resources list has nothing audio-related, and no GPIO is labeled for audio in the interface tables).

**Constraint walkthrough**: mic was to be reserved on I2C (GPIO8/9) initially, which would have left only the RS485 header (GPIO15/16) or CAN header (GPIO19/20) as candidates -- both ruled out: RS485 is described as having "automatic transmit/receive control," implying a dedicated transceiver IC sits between the GPIOs and the header pins (not raw digital I/O); CAN shares GPIO19/20 with native USB via the CH422G's `EXIO5` mux, so using it would sacrifice the USB port this whole project's flash/debug workflow depends on. A third-party board-config file (`iamfaraz/Waveshare_ST7262_LVGL`) was checked as a possible cross-reference for free pins, but ruled out as unreliable for this purpose -- it disables the CH422G expander and backlight entirely (which we proved on real hardware are required), so its "free pins" list only reflects what that particular incomplete example doesn't reference, not the actual physical routing.

**Resolution**: user reassigned the mic to the 3-pin "sensor header" (ADC-connected, per user's own investigation) instead of I2C, freeing GPIO8/9 for the buzzer. Landed on: **PCF8574 I2C GPIO expander module + passive piezo buzzer**, wired to the I2C header. This was chosen over tapping a free-but-unbroken-out GPIO directly on the module (e.g. via RS485 bypass) because it's header-mounted (stability preference stated explicitly) rather than requiring 0.4mm-pitch QFN soldering.

**Important**: the onboard CH422G IO expander is already at I2C address `0x20` (confirmed in `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h`) -- a stock PCF8574 also defaults to `0x20`. **Before wiring, the PCF8574's A0/A1/A2 address jumpers must be changed** (e.g. pull A0 high for `0x21`) to avoid an address collision with the existing expander.

**Status**: hardware not yet acquired/wired. Once wired, firmware needs: a PCF8574 driver call (toggle its output pin over I2C at the new address) wired into `update_prayer_display()` at the moment `next_slot` changes, to produce the 3-beep pattern.

## 20. Full-year prayer time data loaded (2026-08-07)

Replaced the partial 61-day dataset (Aug+Sep 2026 from myrakan.com) with the **full 365-day 2026 calendar** for zone JHR02, sourced from the official **JAKIM e-Solat portal** (https://www.e-solat.gov.my) -- the authoritative Malaysian government source, versus the earlier third-party mirror. Site is dynamic (zone selector + date-range picker + PDF/CSV export), and was unreachable in a way that resisted a simple save -- user selected JHR02 + yearly range and printed the results to PDF (`Portal e-Solat.pdf`, 37 pages) instead.

**Pipeline**: PDF (37 pages, read via the `pages` parameter in 3 batches) -> manually transcribed into `JHR02_2026_full_raw.txt` (raw 12h am/pm format matching the source) -> `convert_full_year.py` (new script: 12h->24h conversion, DD-Mon-YYYY parsing including the Malay month abbreviations Mac/Mei/Ogos/Dis, sanity-checks for exactly 365 rows with no date gaps/duplicates) -> `JHR02_2026_full.csv` -> `csv_to_c_array.py` (updated to prefer the full-year file over per-month files when both exist) -> `prayer_times_data.h` (now 365 rows, ~24KB, still trivial for this board's flash).

**Verification**: cross-checked 2026-08-01 against the original myrakan.com-sourced `JHR02_2026-08.csv` -- values matched exactly. One transcription error was caught and fixed (Dec 31's Hijri date field).

**Firmware simplified accordingly**: since `PRAYER_DOY[i] == i+1` now holds for every row (no gaps), `find_prayer_row_for_doy()` in `PrayerTimes.ino` was simplified from a linear search to a direct array index (with a bounds check retained as a guard against day 366 in a future leap year, since this table has exactly 365 rows). Confirmed on-device: today's prayer times display correctly, next-prayer yellow highlight still works.

**Kept for reference**: the original `JHR02_2026-08.csv` / `JHR02_2026-09.csv` (myrakan.com source) remain on disk in `~/Touchscreen/prayer_times/` -- not deleted, used as the cross-check above and retained per user's request to save data for later reuse/verification.

**Known limitation carried forward**: this table is JAKIM's specific 2026 calculation, reused by day-of-year for any year the board happens to be running in. Prayer times do shift slightly year to year (solar position, leap-year drift) -- fine for this display's purpose, not a claim of exact multi-year accuracy. Regenerate from a fresh JAKIM export if that matters later.

## 21. Per-mode background colors + boxed highlight + two real bugs found and fixed (2026-08-07)

**Cosmetic changes**: each screen now has its own distinct background instead of shared black -- screen 1 (prayer) deep green, screen 2 (analog) very deep blue, screen 3 (digital) very deep red (colors defined once as `BG_PRAYER`/`BG_ANALOG`/`BG_DIGITAL`, applied to the shared `screen` object in `apply_mode_visibility()` on every mode switch). The "next prayer" highlight changed from plain yellow text to a **yellow box behind the row** (new `prayer_row_highlight[]` array of background panels, one per row, toggled opaque/transparent) with **deep green text** on top -- reusing `BG_PRAYER` as the highlighted-row text color, so there's one source of truth for that shade rather than a duplicate constant.

**Two real bugs surfaced during this pass, unrelated to the color changes**:
1. **`apply_mode_visibility()` rendered content before NTP finished syncing on some boots.** It called `update_*_display()` unconditionally, without checking `time_synced` the way the periodic `clock_update_cb()` already did -- on a boot where WiFi/NTP was slow or failed, this could read garbage/uninitialized RTC time and show a wrong date, which only looked "fixed" once something later (e.g. tapping through modes) happened to re-render after time was eventually correct. Fixed by gating `apply_mode_visibility()`'s content-rendering half on `time_synced`, matching `clock_update_cb()`.
2. **`status_label` was a stale, permanently-visible boot-time snapshot.** It was created once with whatever `wifi_status_message` held at boot and left sitting on the shared `screen` (visible across all 3 modes) for the rest of the session, even after a successful sync (empty message = blank-but-present label). Fixed: only created when there's an actual failure message (`wifi_status_message[0] != '\0'`), with a solid background/padding so it's clearly a real alert when it does appear, and `lv_obj_move_foreground()` so it stays on top regardless of mode.

Verified via a real failed-boot case: one power cycle genuinely failed to sync NTP in time (transient WiFi issue) -- with the fix, this now correctly shows the failure message and skips rendering blank/garbage time, rather than the old behavior of silently showing wrong-looking data. A later successful boot confirmed clean sync with no stray message.

## 22. Home-screen dial spacing + after-Isyak highlight wrap (2026-08-07)

Two more tweaks to the prayer/home screen:
- **Analog dial vertical spacing**: the small clock meter in block 2 had a visible gap above it but touched the digital time readout below with no gap -- nudged up ~14px (half of the `montserrat_28` readout font's size) to balance the spacing.
- **No highlight after Isyak**: the "next prayer" search only looked for a time later *today*; once Isyak passed, nothing matched and `next_slot` stayed unset, so no row was highlighted at all for the rest of the night. Fixed: when nothing later remains today, wrap to tomorrow's Subuh -- always visible-slot 0, since Subuh is first in `VISIBLE_PRAYER_COLS` regardless of the day. Verified live (real device clock was past today's Isyak at test time): Subuh now correctly highlights.

## 23. Background color follow-up: screen 2 reverted to black, screen 1 darkened (2026-08-07)

Screen 2's (analog) deep-blue background was reverted to plain black -- the analog meter widget's own dial (`build_clock_meter()`) has its own separately-styled black `LV_PART_MAIN` background (see #14), and against a non-black screen background that black circle read as a visual defect ("ugly floating circle") rather than a clock face. Screen 3 (digital) keeps its deep red, since it has no competing black element behind it. Screen 1 (prayer)'s green was darkened further, from `LV_COLOR_MAKE(0x00, 0x2A, 0x0E)` to `LV_COLOR_MAKE(0x00, 0x18, 0x08)`, "very dark green" per request.

Considered adding **bold weight** to the highlighted prayer row's text (on top of the existing yellow-box + deep-green-text treatment). Not implemented: LVGL's bundled Montserrat fonts ship regular weight only (no bold variant), and generating a custom bold font needs `lv_font_conv` (Node/npm), unavailable in this dev environment (same constraint hit earlier when considering a custom large font for the digital clock, see #14). User agreed the existing box+color emphasis is sufficient without it.

## 24. Diagnosed: M5Stack Grove module on I2C header caused display to go dark -- wrong signal type, not voltage (2026-08-07)

User connected an M5Stack Grove module to this board's I2C header (same header planned for the PCF8574 buzzer, see #19) and observed: display went dark immediately on connecting; disconnecting, the board reset and recovered normally with no lasting damage.

**Initial hypothesis (raised, then superseded)**: suspected a 3.3V/5V voltage mismatch, since this board's I2C header has a documented "I2C level selection" jumper (3.3V or 5V) and M5Stack/Grove modules vary in their voltage requirements, while GT911 touch + CH422G expander share that same GPIO8/9 bus.

**Actual root cause, once identified**: the specific module was a **Grove analog microphone**, not an I2C device at all. The Grove 4-pin connector standard reuses the same physical pins (VCC/GND/SIG1/SIG2) for entirely different electrical purposes depending on the module -- on I2C modules, SIG1/SIG2 carry SDA/SCL digital signaling; on analog modules like this mic, SIG1 carries a raw continuously-varying analog voltage. Plugging an analog-signal device into a header wired for I2C protocol (with pull-ups, expecting clean digital SDA/SCL transitions) put a continuously-varying analog signal onto the same bus as the touch controller and IO expander -- almost certainly disrupting their I2C transactions and causing the display to go dark. Not a voltage-magnitude problem, and not damage -- a protocol/signaling conflict that cleared immediately on disconnect, consistent with what was observed.

**Resolution**: this mic was always intended for the board's separate 3-pin **ADC-connected sensor header** (per user's earlier plan, see #19), not the I2C header -- the original plan was correct, this was just a mis-wire during hardware bring-up. Move the mic to the sensor header; the I2C header remains reserved for the PCF8574 buzzer as planned. No jumper investigation needed for this specific issue, though verifying the I2C header's 3.3V/5V jumper position is still worthwhile before connecting the PCF8574, as ordinary I2C-device hygiene.

## Next step

`ClockDisplay` (clock-only) and `PrayerTimes` (prayer dashboard + clock, full-year data, 6 visible prayer rows, per-mode background colors, boxed highlight) are two separate, independently-developed sketches, both confirmed working on-device. Open items: (1) mic needs to be moved to the sensor header, not I2C (see #24); (2) prayer-time audio alert -- GPIO/hardware plan settled (PCF8574 on I2C header, see #19), not yet wired/coded; (3) v3 photo frame mode (touch-selectable, extending the same mode-cycle pattern). Prayer time data is complete for all of 2026 -- no more months needed unless regenerating for a different year.

