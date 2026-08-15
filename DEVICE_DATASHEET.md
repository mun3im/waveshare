# Waveshare ESP32-S3-Touch-LCD-7 — Diagnostics Datasheet

Compiled from live USB/serial/flash diagnostics on 2026-08-07.
Board model: **ESP32-S3 7" Touch Display, 800×480, Xtensa LX7 dual-core, 8MB Flash (nominal), WiFi + BT5, GUI/LVGL/HMI**.

Status: **working**. The dark display on arrival was a `PSRAM=disabled` build-config
issue, not a hardware fault — see §9.

These are measured values from this specific board. For the manufacturer's own
documentation — wiki, schematic, and the ST7262 / CH422G / GT911 datasheets — see
[README.md](README.md#official-documentation).

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

## 9. Development log summary (2026-08-07)

Sections 1–8 above are the board's measured hardware facts. What follows condenses the
bring-up and development history into the findings that still matter. The original
per-session log is preserved in git history (commit `1b4023e` and earlier).

### Build settings — the critical ones

**`PSRAM=enabled` is mandatory for any sketch that drives the LCD.** The ST7262 RGB panel
needs an 800×480×3×2 ≈ 2.3 MB frame buffer, which cannot fit in the ESP32-S3's ~390 KB of
usable internal SRAM and must come from PSRAM. The Arduino board definition defaults PSRAM
to *disabled*, and the failure chain is opaque: frame buffer allocation fails → LCD init
fails → LVGL init fails → the sketch dereferences an uninitialized LVGL mutex → panic.
Because the panic lands mid-way through USB stack bring-up, the native USB-JTAG port also
stops enumerating, which makes the board look bricked when it isn't.

**Use a 16 MB partition scheme.** Full options:

```
FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled
```

The board has 16 MB of flash (§3), but Arduino's default scheme assumes 8 MB with a
1.25 MB app slot — too small once WiFi and LVGL are both linked in. `app3M_fat9M_16MB`
gives 3 MB for the app plus ~9.9 MB FATFS. Trade-off: single app partition, so no dual
OTA. Fine for a USB-flashed hobby project.

**LVGL v8, not v9.** `ESP32_Display_Panel`'s port example uses LVGL v8 driver APIs
(`lv_disp_drv_t`, `lv_indev_drv_t`) that v9 removed. The globally-installed `lvgl` library
had to be pinned to 8.4.0. This is a machine-wide setting — another sketch needing v9 APIs
would conflict.

**Board selection**: set `ESP_PANEL_BOARD_DEFAULT_USE_SUPPORTED` to `1` and uncomment
`#define BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7` in `esp_panel_board_supported_conf.h`
(copied per-sketch from the library example, not edited in the shared library install).

### The WiFi + RGB LCD cache race

Calling `WiFi.begin()` *after* the LCD and LVGL are initialized crashes deterministically,
every boot:

```
Guru Meditation Error: Core 1 panic'ed (Cache disabled but cached memory region accessed)
EXCCAUSE: 0x00000007 (LoadStoreError)
```

The RGB LCD driver's bounce-buffer refresh ISR reads cached PSRAM; `WiFi.begin()` briefly
disables the flash cache for its own flash/NVS access. If the ISR fires during that
window, it faults. Upstream issue:
[esp-arduino-libs/ESP32_Display_Panel#198](https://github.com/esp-arduino-libs/ESP32_Display_Panel/issues/198).

**Workaround used in every sketch here**: connect WiFi and complete the NTP sync in
`setup()` *before* initializing the board/LCD/LVGL, so no bounce-buffer ISR exists while
WiFi touches the cache. Pure code reordering, no special build flags.

**Caveat**: this is a workaround, not a fix. Any WiFi/NVS/OTA call made *after* the display
is running reintroduces the crash. Proper fixes would be enabling XIP-from-PSRAM
(`CONFIG_SPIRAM_XIP_FROM_PSRAM`, an ESP-IDF Kconfig option not exposed in the stock Arduino
board menu), or pausing the LVGL refresh timer around such calls.

### Hardware notes

- **Backlight and reset are not GPIOs.** `LCD_BL`, `LCD_RST`, `TP_RST`, `SD_CS`, and
  `USB_SEL` sit behind a **CH422G I2C IO expander** at address `0x20` (SCL=GPIO9,
  SDA=GPIO8): `TP_RST`=1, `LCD_BL`=2, `LCD_RST`=3, `SD_CS`=4, `USB_SEL`=5. A naive
  GPIO-only backlight sketch cannot work. `ESP32_Display_Panel` handles this correctly.
- **Second USB-C port.** A separate CH34x USB-UART bridge (`/dev/cu.wchusbserial*`) is
  independent of the ESP32-S3's native USB. It stays alive when the native port is wedged
  by a crash — invaluable for reading logs and reflashing during a bad boot loop.
- **microSD slot is physically inaccessible.** It sits underneath the LCD module; reaching
  it means unscrewing the panel or working a card in with tweezers. Not a defect, just a
  design choice.
- **No onboard audio.** No speaker, buzzer, or I2S hardware anywhere on the board. A prayer
  alert tone would need external hardware — the plan was a PCF8574 I2C expander plus a
  passive piezo on the I2C header. **If wiring a PCF8574, change its A0/A1/A2 address
  jumpers first**: it defaults to `0x20`, colliding with the onboard CH422G.
- **The I2C header speaks I2C only.** Connecting a Grove *analog* module (a mic) to it put
  a continuously-varying analog voltage on the SDA/SCL lines shared with the GT911 touch
  controller and CH422G expander, disrupting their transactions and blanking the display.
  Recovered fully on disconnect — a protocol mismatch, not damage or a voltage problem.
  The Grove 4-pin connector reuses the same physical pins for entirely different electrical
  purposes depending on the module. Analog sensors belong on the 3-pin ADC sensor header.

### LVGL v8 gotchas hit while building the clocks

- **Zoomed labels don't self-center.** `lv_obj_set_style_transform_zoom()` scales only what
  is *drawn* — the layout box keeps its pre-zoom size, and v8's default pivot is the
  top-left corner. Montserrat digits also aren't fixed-width, so a clock label's rendered
  width changes every second. Labels must be re-measured and re-centered on *every* text
  update, not once at creation.
- **Meter ticks vs. needle angles.** `lv_meter` spaces N ticks across N−1 gaps, while a
  needle's angle maps across the full `(max − min)` range. With `tick_cnt = 60` over a
  `0..60` range the two disagree, and the error accumulates around the dial (worst at the
  bottom). Use `tick_cnt = 61` so `61 − 1 == 60` matches the value range.
- **Widgets swallow taps.** `lv_meter` is clickable by default in v8 and will consume touch
  events before they reach a screen-level handler. Clear `LV_OBJ_FLAG_CLICKABLE` on it.
- **No bold font.** LVGL's bundled Montserrat fonts are regular weight only; generating a
  bold variant needs `lv_font_conv` (Node/npm).

### Factory image

The board arrived with an `app0` that only ran `Serial.begin(115200)` and printed
`Hello World!` in a loop — no LCD, touch, or expander code. Almost certainly a QC-test
image from a previous owner or reseller, not Waveshare's LVGL demo, which is why restoring
it still left the display dark. `app1` held no valid backup. Nothing genuine was lost.
Waveshare publishes no GitHub repo for the plain (non-B/C) ESP32-S3-Touch-LCD-7.

### Current state

`PrayerTimes`, `ClockDisplay`, and `BouncingBall` are all confirmed working on hardware.
`PrayerTimes` compiles to ~1.39 MB, 44% of the 3 MB app partition (verified 2026-08-15
with `arduino-cli` 1.5.1 and the FQBN options above).

Open items:
1. Prayer-time audio alert — hardware plan settled (PCF8574 + piezo on the I2C header),
   not yet wired or coded.
2. Photo-frame mode for `PrayerTimes`, extending the existing tap-cycle pattern.
3. Prayer time data covers all of 2026 and is reused by day-of-year for other years. Times
   drift slightly year to year; regenerate from a fresh JAKIM export if exact accuracy
   matters.
