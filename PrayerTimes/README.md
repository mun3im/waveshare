# PrayerTimes — Waktu Solat display for Waveshare ESP32-S3-Touch-LCD-7

A touchscreen prayer-times clock for the Waveshare ESP32-S3-Touch-LCD-7 (800×480 RGB
LCD). It shows today's prayer times for JAKIM zone **JHR02** (Johor Bahru, Kota Tinggi,
Mersing, Kulai) alongside a clock, highlighting whichever prayer is next.

The whole year's prayer times are compiled into the firmware as a lookup table, so the
device needs the network only once at boot — to set the clock over NTP. There is no
runtime API call, and no connectivity is needed after that.

## Screens

Tap anywhere to cycle through three modes:

1. **Prayer (home / default)** — title bar on top; left half has a small analog clock,
   the date, and a monospace `HH:MM:SS`; right half lists the day's prayer times with
   the next upcoming one highlighted in yellow. Dark green background.
2. **Analog** — full-screen analog clock face. Black background.
3. **Digital** — large `HH:MM:SS` with the date beneath it. Deep red background.

Six prayers are listed: Subuh, Syuruk, Zuhur, Asar, Maghrib, Isyak. Imsak is in the
data table but deliberately left off the display. Times render as 12-hour without an
AM/PM suffix, which is unambiguous in context. After Isyak, the highlight wraps to the
next day's Subuh.

## Behavior notes

- Local time is fixed to **GMT+8, no DST**; NTP from `pool.ntp.org` / `time.google.com`.
- WiFi and NTP run in `setup()` *before* the LCD and LVGL are brought up. This ordering
  is deliberate: it avoids a known ESP32-S3 RGB-LCD bounce-buffer + WiFi flash-cache
  race ([ESP32_Display_Panel#198](https://github.com/esp-arduino-libs/ESP32_Display_Panel/issues/198)).
- Time sync is attempted once, at boot, with no retry. If it fails, a status message is
  overlaid on all screens and the clock/prayer content stays blank rather than rendering
  a garbage RTC time.
- The table holds 365 rows. Day 366 of a future leap year falls through the bounds check
  in `find_prayer_row_for_doy()` and shows "no data" rather than reading past the array.

## Source of prayer times data

Prayer times come from the **JAKIM e-Solat portal** — the official source published by
Jabatan Kemajuan Islam Malaysia:

- **Portal**: <https://www.e-solat.gov.my/index.php?siteId=24&pageId=24>
- **Zone**: JHR02 — Johor Bahru, Kota Tinggi, Mersing, Kulai
- **Year**: 2026, full year (365 days), yearly ("Tahunan") export

The portal is JS-driven and doesn't fetch cleanly, so the yearly result page was printed
to `Portal e-Solat.pdf` (37 pages, saved 2026-08-07), kept in this folder as the archived
source document.

A second, unofficial source — [myrakan.com/waktusolat](https://myrakan.com/waktusolat) —
supplied two months of data before the full JAKIM export was obtained. It was used only
to cross-check the transcription (2026-08-01 matched exactly), never as firmware input,
and has been superseded by the full-year export.

**Accuracy caveat**: prayer times shift slightly year to year for the same calendar date.
This table is JAKIM's 2026 calculation, indexed by day-of-year and reused for subsequent
years. That's a fine approximation for a display clock, not an exact-accuracy claim
outside 2026. Regenerate from a fresh JAKIM export if precision matters.

## Data pipeline

Everything needed to regenerate the data table lives in `prayer_times/` in this folder —
the sketch is self-contained. See `prayer_times/JHR02_2026_notes.md` for the full
provenance record. The chain is:

```
Portal e-Solat.pdf                        (JAKIM yearly export, archived in sketch root)
  → prayer_times/JHR02_2026_full_raw.txt  manually transcribed, 12h am/pm as on screen
  → prayer_times/convert_full_year.py     → prayer_times/JHR02_2026_full.csv (24h, ISO)
  → prayer_times/csv_to_c_array.py        → prayer_times_data.h (minutes past midnight)
```

`prayer_times/` is a plain subfolder, not `src/`, so the Arduino IDE ignores it when
compiling — the pipeline ships alongside the firmware without becoming part of the build.
`csv_to_c_array.py` writes its header up into the sketch root, and `prayer_times/` keeps
no copy of its own, so exactly one `prayer_times_data.h` exists in the tree.

`convert_full_year.py` asserts exactly 365 sequential dates with no gaps or duplicates.
`csv_to_c_array.py` emits `PRAYER_MINUTES[365][7]` plus a parallel `PRAYER_DOY[]`
day-of-year array, so the firmware can distinguish "no data for this day" from a real
row. It prefers the full-year CSV and falls back to per-month files if that's absent.

To regenerate after changing the source data:

```
cd prayer_times
python3 convert_full_year.py
python3 csv_to_c_array.py
```

No copy step — the second script writes `prayer_times_data.h` to the sketch root directly.
That file is generated; don't hand-edit it.

## Building

Arduino sketch, no build system. Compile with the Arduino IDE, or headlessly:

```
arduino-cli compile --fqbn "espressif:esp32:waveshare_esp32_s3_touch_lcd_7:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled" PrayerTimes
```

Those three options are all required — see the Hardware notes above. Flashing adds
`-u -p <port>`. Verification means running it on the actual board.

**Dependencies**: `ESP32_Display_Panel` and `lvgl` (v8). The board config headers
(`esp_panel_board_*_conf.h`, `esp_panel_drivers_conf.h`, `lv_conf.h`, `lvgl_v8_port.*`)
are vendored here from the library's `simple_port` example, which handles bring-up for
this exact board: RGB LCD + CH422G IO expander + GT911 touch.

**WiFi credentials**: `wifi_credentials.h` is gitignored. Create it with:

```c
#pragma once
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
```

## Hardware notes

[`../DEVICE_DATASHEET.md`](../DEVICE_DATASHEET.md) is the running diagnostics log for
this board — chip/flash
identification, partition layout, and writeups of every non-obvious problem hit during
bring-up. The two settings most likely to cost you an afternoon:

- **Build with `PSRAM=enabled`.** The 800×480 RGB panel needs a ~2.3 MB frame buffer that
  can only come from PSRAM, and the Arduino board definition defaults it to *disabled* —
  producing a dark screen and a boot crash.
- **Use a 16 MB partition scheme** (`FlashSize=16M,PartitionScheme=app3M_fat9M_16MB`).
  The default 1.25 MB app slot is too small once WiFi and LVGL are both linked in.

It's a dated engineering log covering several sketches on this board, so some paths in
older entries refer to earlier project layouts.

## Origin

Forked from a generic ClockDisplay sketch on 2026-08-07 and turned into a dedicated
prayer-times app.
