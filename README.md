# Waveshare ESP32-S3-Touch-LCD-7 sketches

Arduino sketches for the Waveshare ESP32-S3-Touch-LCD-7 — a 7" 800×480 RGB touch panel
on an ESP32-S3, with a GT911 touch controller and a CH422G IO expander driving the
backlight and reset lines.

[`DEVICE_DATASHEET.md`](DEVICE_DATASHEET.md) is the running diagnostics log for the
board: chip and flash identification, partition layout, and writeups of each non-obvious
problem hit during bring-up. Read it before starting anything new here.

## Projects

| Sketch | What it does |
|---|---|
| [`PrayerTimes/`](PrayerTimes/) | Waktu solat display for JAKIM zone JHR02 (Johor Bahru), with a full year of prayer times compiled in. Tap cycles prayer / analog / digital screens. Has its own [README](PrayerTimes/README.md). |
| [`ClockDisplay/`](ClockDisplay/) | Full-screen clock; tap toggles between a large digital readout and an analog face. `PrayerTimes` was forked from this. [README](ClockDisplay/README.md) |
| [`BouncingBall/`](BouncingBall/) | The LVGL demo that first proved the panel working, later extended with WiFi + NTP. The known-good baseline for this board. [README](BouncingBall/README.md) |

[`Working_LCD7_Serialtest/`](Working_LCD7_Serialtest/) is a minimal smoke test: serial
echo at 115200 baud, no libraries, no display code
([README](Working_LCD7_Serialtest/README.md)). Use it to confirm a board is alive and the
USB serial path works before debugging anything more complicated. It also has some
history — the datasheet's "Factory image" note identifies its `Hello World!` output as
what this board's "factory" image was actually printing.

`wiki.pdf` is Waveshare's vendor documentation for the board.

The other bring-up probes from the dark-screen investigation were removed: they were
written against `TFT_eSPI` before the panel was identified, and this board has an **RGB
parallel** panel (ST7262 behind a CH422G IO expander), not an SPI one. They could never
have worked here, and kept suggesting wrong controllers (ILI9488, ILI9806E). The
investigation itself is written up in the datasheet, which is the part worth keeping.

## Official documentation

Waveshare's own resources for this board:

- **[Wiki page](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)** — pinouts, onboard
  resource list, demo code, and links to every component datasheet below. Start here.
- **[Product page](https://www.waveshare.com/esp32-s3-touch-lcd-7.htm)** — specs and
  what's in the box.
- **[Schematic (PDF)](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-7/ESP32-S3-Touch-LCD-7-Sch.pdf)**
  — the authority on what is actually wired where, including which lines run through the
  IO expander rather than straight to a GPIO.

Component datasheets, all mirrored by Waveshare:

| Part | Role on this board |
|---|---|
| [ST7262](https://files.waveshare.com/wiki/common/ST7262.pdf) | The RGB parallel LCD controller. Not SPI — this is why `TFT_eSPI`-style sketches cannot drive this panel. |
| [CH422G](https://files.waveshare.com/wiki/common/CH422DS1_EN.pdf) | I2C IO expander at `0x20` holding the backlight, LCD reset, touch reset, SD CS, and USB select lines. |
| [GT911](https://files.waveshare.com/wiki/common/GT911_EN_Datasheet.pdf) | Capacitive touch controller, on the same I2C bus as the expander. |
| [ESP32-S3-WROOM-1](https://files.waveshare.com/wiki/common/Esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) | The module itself. See also the [ESP32-S3 datasheet](https://files.waveshare.com/wiki/common/Esp32-s3_datasheet_en.pdf) and [technical reference manual](https://files.waveshare.com/wiki/common/Esp32-s3_technical_reference_manual_en.pdf). |

A local copy of the vendor documentation is also checked in as `wiki.pdf`.

Note that Waveshare publishes **no GitHub repository** for the plain (non-B/C)
ESP32-S3-Touch-LCD-7 — [`waveshareteam`](https://github.com/waveshareteam) has repos for
other boards, but not this one. The working driver stack for it is Espressif's
[`ESP32_Display_Panel`](https://github.com/esp-arduino-libs/ESP32_Display_Panel), which
all the LVGL sketches here build on.

## Build settings

Every sketch that drives the LCD needs these three options — the defaults fail in ways
that are hard to diagnose:

```
FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled
```

- **`PSRAM=enabled`** — the RGB panel needs a ~2.3 MB frame buffer that can only come
  from PSRAM. The Arduino board definition defaults it to *disabled*, which yields a
  dark screen and a boot crash (see the datasheet's "Build settings").
- **16 MB partition scheme** — the default 1.25 MB app slot overflows once WiFi and LVGL
  are both linked in (same section).

Full example:

```
arduino-cli compile --fqbn "espressif:esp32:waveshare_esp32_s3_touch_lcd_7:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled" PrayerTimes
```

Add `-u -p <port>` to flash. These sketches target **LVGL v8** — v9 removed the driver
APIs the `ESP32_Display_Panel` port relies on.

## WiFi credentials

Sketches that use WiFi read `wifi_credentials.h`, which is gitignored. Copy the template
and fill it in locally:

```
cp wifi_credentials.h.example wifi_credentials.h
```

The board's radio is 2.4 GHz only.
