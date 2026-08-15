# ClockDisplay

A full-screen clock for the 800×480 panel. Tap anywhere to toggle between a large digital
readout and an analog clock face.

`PrayerTimes` was forked from this sketch; this one stays clock-only.

## What it does

- Connects to WiFi and syncs the clock over NTP at boot (GMT+8, no DST).
- **Digital mode**: `HH:MM:SS` in Montserrat 48 scaled ~2.7× via LVGL's zoom transform,
  with the full date beneath it.
- **Analog mode**: an `lv_meter` clock face with 60 tick marks and hour/minute/second
  needles.
- Tapping anywhere on the screen switches modes.
- Shows a status label if WiFi or NTP failed.

## Two LVGL gotchas worth knowing

Both were real bugs here, and both are easy to hit again in any LVGL v8 clock:

**Zoomed labels don't self-center.** `lv_obj_set_style_transform_zoom()` scales only what
gets *drawn* — the object's layout box stays at its pre-zoom size, and v8's default zoom
pivot is the top-left corner, not the center. Worse, Montserrat digits aren't fixed-width
("1" is narrower than "8"), so the rendered width changes every second. The label must be
re-measured and re-centered on *every* text update, not once at creation.

**Meter ticks and needles use different angle math.** LVGL v8 spaces N ticks across N−1
gaps, while a needle's angle is mapped across the full `(max − min)` value range. With
`tick_cnt = 60` over a `0..60` range those disagree, and the error accumulates around the
dial — most visibly at the bottom. Use `tick_cnt = 61` so `61 − 1 == 60` matches the value
range exactly.

## Why the boot order looks backwards

WiFi and NTP complete *before* the LCD and LVGL are initialized, to avoid a cache race
between the RGB panel's bounce-buffer ISR and `WiFi.begin()`. See
`../DEVICE_DATASHEET.md` §12.

## Building

```
arduino-cli compile --fqbn "espressif:esp32:waveshare_esp32_s3_touch_lcd_7:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled" ClockDisplay
```

All three options are required: `PSRAM=enabled` for the panel's frame buffer, and the
16 MB scheme because this build overflows the default 1.25 MB app slot once WiFi and LVGL
are both linked in. Copy `wifi_credentials.h.example` to `wifi_credentials.h` first.
Targets **LVGL v8**.
