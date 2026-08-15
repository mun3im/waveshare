# Working_LCD7_Serialtest

Minimal smoke test: prints `Hello World!` at boot, then echoes back anything received on
the serial port. No libraries, no display code, nothing to misconfigure.

## When to use it

Flash this first when a board is behaving strangely. It answers one question cleanly —
*is the board alive and is the USB serial path working?* — without involving PSRAM, the
RGB panel, the IO expander, or LVGL, any of which can independently produce a dark screen
that looks like a dead board.

If this prints and echoes, the chip and USB path are fine and the problem is further up
the stack. If it doesn't, nothing more complicated is worth trying yet.

Connect at **115200 baud**.

## A note on where it came from

The `Hello World!` output has some history. This board arrived with an `app0` image that
did nothing but `Serial.begin(115200)` and print `Hello World!` in a loop — no LCD, no
touch, no expander. It was almost certainly a minimal QC-test image from a previous owner
or reseller, not Waveshare's actual LVGL demo, which is why restoring the "factory"
firmware still left the display dark. See the "Factory image" note in
`../DEVICE_DATASHEET.md`.

## Building

No special options needed — this sketch never touches the panel:

```
arduino-cli compile --fqbn espressif:esp32:waveshare_esp32_s3_touch_lcd_7 Working_LCD7_Serialtest
```

Add `-u -p <port>` to flash. The comments in the `.ino` mention RS485; it's plain
`Serial`, inherited from the Waveshare example this was adapted from.
