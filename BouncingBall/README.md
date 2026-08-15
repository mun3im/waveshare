# BouncingBall

A red ball bouncing around the 800×480 panel with a live NTP clock riding inside it.

This is the sketch that first proved the display working on this board, so it doubles as
the known-good baseline: if something stops working here, the problem is the board or the
build settings, not your application code.

## What it does

- Connects to WiFi and syncs the clock over NTP at boot (GMT+8, no DST).
- Draws a 140px red circle that bounces off all four screen edges, advanced every 16 ms
  (~60 FPS) by an LVGL timer.
- Centers an `HH:MM:SS` label inside the ball, refreshed once a second, so it moves with
  the ball.
- Shows a status label if WiFi or NTP failed; blank on success.

## Why the boot order looks backwards

`setup()` connects to WiFi and finishes the NTP sync *before* initializing the LCD or
LVGL. That ordering is deliberate and load-bearing.

The ESP32-S3's RGB LCD driver uses a bounce-buffer refresh ISR that reads cached PSRAM,
while `WiFi.begin()` briefly disables the flash cache for its own flash/NVS access. If
both run at once, the ISR can fire mid-cache-disable and panic with *"Cache disabled but
cached memory region accessed."* Doing the network step first — before any LCD timers
exist — sidesteps the race entirely.

This is a workaround, not a fix. If you extend this sketch to touch WiFi, NVS, or OTA
*after* the display is running, the crash risk returns. See "The WiFi + RGB LCD cache
race" in `../DEVICE_DATASHEET.md` for the full writeup and upstream issue.

## Building

```
arduino-cli compile --fqbn "espressif:esp32:waveshare_esp32_s3_touch_lcd_7:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled" BouncingBall
```

`PSRAM=enabled` is mandatory — without it the panel can't allocate its frame buffer and
you get a dark screen and a boot crash. Copy `wifi_credentials.h.example` to
`wifi_credentials.h` and fill in your network first. Targets **LVGL v8**.
