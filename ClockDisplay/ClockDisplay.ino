/**
 * Large Clock Display for Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Full-screen clock, digital <-> analog toggled by tapping anywhere on screen.
 * (Prayer-times mode was split out into the separate `PrayerTimes` sketch, a
 * fork of this one from 2026-08-07. This
 * sketch stays a clean, minimal clock-only build.)
 *
 * Built on top of the ESP32_Display_Panel "simple_port" example, which handles
 * board bring-up (RGB LCD + CH422G IO expander + GT911 touch) and LVGL porting
 * for this exact board (see esp_panel_board_supported_conf.h ->
 * BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7).
 *
 * On boot: connects to WiFi and syncs time via NTP (local time = GMT+8, no DST)
 * *before* touching the LCD/LVGL -- see the comment in setup() for why (a known
 * ESP32-S3 RGB-LCD + WiFi cache-race crash, see DEVICE_DATASHEET.md).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "wifi_credentials.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// Local time is GMT+8, no daylight saving.
static const long GMT_OFFSET_SEC = 8 * 3600;
static const int DST_OFFSET_SEC = 0;
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.google.com";

static const int32_t SCREEN_W = 800;
static const int32_t SCREEN_H = 480;

static bool time_synced = false;
static char wifi_status_message[64] = "";

// ---- UI objects ----
static lv_obj_t *screen;
static lv_obj_t *status_label;

// Digital mode
static lv_obj_t *digital_group;
static lv_obj_t *digital_time_label;
static lv_obj_t *digital_date_label;

// Analog mode
static lv_obj_t *analog_meter;
static lv_meter_indicator_t *hand_hour;
static lv_meter_indicator_t *hand_min;
static lv_meter_indicator_t *hand_sec;

static bool analog_mode = false;
static lv_timer_t *clock_timer;

static void format_time_strings(char *time_buf, size_t time_len, char *date_buf, size_t date_len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(time_buf, time_len, "%H:%M:%S", &timeinfo);
    strftime(date_buf, date_len, "%A, %d %B %Y", &timeinfo);
}

// LVGL's Montserrat digits are not fixed-width (e.g. "1" is narrower than "8"),
// so the label's rendered width changes as the digits change. Combined with the
// zoom transform (which only scales what's *drawn*, not the layout box used for
// positioning -- see build_digital_group()), the label needs to be re-measured
// and re-centered after every text change, not just once at creation.
static void recenter_time_label()
{
    lv_obj_update_layout(digital_time_label);
    int32_t w = lv_obj_get_width(digital_time_label);
    int32_t h = lv_obj_get_height(digital_time_label);
    lv_obj_set_style_transform_pivot_x(digital_time_label, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(digital_time_label, h / 2, 0);
    lv_obj_set_pos(digital_time_label, (SCREEN_W - w) / 2, (SCREEN_H - h) / 2 - 40);
}

static void update_digital_display()
{
    char time_buf[16];
    char date_buf[48];
    format_time_strings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    lv_label_set_text(digital_time_label, time_buf);
    recenter_time_label();
    lv_label_set_text(digital_date_label, date_buf);
}

static void update_analog_display()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour12 = timeinfo.tm_hour % 12;
    lv_meter_set_indicator_value(analog_meter, hand_hour, hour12 * 5 + timeinfo.tm_min / 12);
    lv_meter_set_indicator_value(analog_meter, hand_min, timeinfo.tm_min);
    lv_meter_set_indicator_value(analog_meter, hand_sec, timeinfo.tm_sec);
}

static void clock_update_cb(lv_timer_t *timer)
{
    if (!time_synced) {
        return;
    }
    if (analog_mode) {
        update_analog_display();
    } else {
        update_digital_display();
    }
}

static void apply_mode_visibility()
{
    if (analog_mode) {
        lv_obj_add_flag(digital_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(analog_meter, LV_OBJ_FLAG_HIDDEN);
        update_analog_display();
    } else {
        lv_obj_clear_flag(digital_group, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(analog_meter, LV_OBJ_FLAG_HIDDEN);
        update_digital_display();
    }
}

static void screen_tap_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    analog_mode = !analog_mode;
    apply_mode_visibility();
}

static lv_obj_t *build_digital_group(lv_obj_t *parent)
{
    lv_obj_t *group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, SCREEN_W, SCREEN_H);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_CLICKABLE);

    // Largest built-in LVGL font is 48px; zoom it up ~2.7x so "HH:MM:SS" fills
    // most of the screen width on this 800x480 panel.
    //
    // NOTE: lv_obj_set_style_transform_zoom() only scales what's *drawn* -- the
    // object's layout box (used by lv_obj_align/align_to) stays at the pre-zoom
    // size, and LVGL 8's default zoom pivot is the top-left corner, not the
    // center. Both of those need to be handled manually (see recenter_time_label())
    // to get true centering, and it must be re-run on every text change: Montserrat
    // digits aren't fixed-width (e.g. "1" is narrower than "8"), so the label's
    // rendered width -- and therefore where "centered" is -- shifts every second.
    static const int32_t ZOOM = 700; // 256 = 1x

    digital_time_label = lv_label_create(group);
    lv_obj_set_style_text_color(digital_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(digital_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_transform_zoom(digital_time_label, ZOOM, 0);
    lv_label_set_text(digital_time_label, "--:--:--");
    recenter_time_label();

    // Font height is constant regardless of which digits are showing (only width
    // varies), so the date label's vertical position can be computed once here
    // from the sample text's height and doesn't need to move on every update.
    lv_obj_update_layout(digital_time_label);
    int32_t unzoomed_h = lv_obj_get_height(digital_time_label);
    int32_t zoomed_h = unzoomed_h * ZOOM / 256;
    int32_t time_visual_bottom = (SCREEN_H - unzoomed_h) / 2 - 40 + (unzoomed_h - zoomed_h) / 2 + zoomed_h;

    digital_date_label = lv_label_create(group);
    lv_obj_set_style_text_color(digital_date_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(digital_date_label, &lv_font_montserrat_28, 0);
    lv_obj_set_width(digital_date_label, SCREEN_W);
    lv_obj_set_style_text_align(digital_date_label, LV_TEXT_ALIGN_CENTER, 0);
    // Full-width + centered text alignment means this stays correctly centered
    // regardless of how long the actual date string turns out to be.
    lv_obj_set_pos(digital_date_label, 0, time_visual_bottom + 100);
    lv_label_set_text(digital_date_label, "");

    return group;
}

static lv_obj_t *build_analog_meter(lv_obj_t *parent)
{
    int32_t diam = SCREEN_H - 40;

    lv_obj_t *meter = lv_meter_create(parent);
    lv_obj_set_size(meter, diam, diam);
    lv_obj_center(meter);
    lv_obj_remove_style(meter, NULL, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(meter, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN);

    lv_obj_clear_flag(meter, LV_OBJ_FLAG_CLICKABLE);

    // NOTE: LVGL 8's meter widget spaces N ticks across N-1 gaps, but a needle's
    // angle is lv_map()'d across the full (max - min) value range -- for a 360deg
    // scale those two don't agree unless tick_cnt == (max - min) + 1. With
    // tick_cnt=60 over a 0..60 range, ticks landed ~1/59 short of the needle's
    // per-unit angle each step; the error accumulates going around the circle and
    // was most visible in the bottom half (furthest from the shared 0/60 seam at
    // the top). Fix: use tick_cnt = 61 so tick_cnt - 1 == 60 == the value range;
    // ticks 0 and 60 land on the same angle (top), which is correct for a clock.
    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    lv_meter_set_scale_ticks(meter, scale, 61, 2, 8, lv_palette_main(LV_PALETTE_GREY));
    lv_meter_set_scale_major_ticks(meter, scale, 5, 3, 16, lv_color_white(), 12);
    lv_meter_set_scale_range(meter, scale, 0, 60, 360, 270);

    hand_sec = lv_meter_add_needle_line(meter, scale, 2, lv_palette_main(LV_PALETTE_RED), -20);
    hand_min = lv_meter_add_needle_line(meter, scale, 5, lv_color_white(), -30);
    hand_hour = lv_meter_add_needle_line(meter, scale, 8, lv_color_white(), -50);

    return meter;
}

// Runs before the LCD/LVGL are initialized (see setup() for why), so this must not
// touch any LVGL API -- it only sets `time_synced` and `wifi_status_message`, which
// setup() reads afterward when building the UI.
static void connect_wifi_and_sync_time_headless()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connect failed, continuing without network time.");
        snprintf(wifi_status_message, sizeof(wifi_status_message), "WiFi failed - no network time");
        return;
    }

    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());

    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo;
    start = millis();
    bool got_time = false;
    while (millis() - start < 15000) {
        if (getLocalTime(&timeinfo, 1000)) {
            got_time = true;
            break;
        }
        Serial.println("Waiting for NTP time sync...");
    }

    if (got_time) {
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S (GMT+8)", &timeinfo);
        Serial.print("Time synced: ");
        Serial.println(buf);
        time_synced = true;
        wifi_status_message[0] = '\0';
    } else {
        Serial.println("NTP sync failed.");
        snprintf(wifi_status_message, sizeof(wifi_status_message), "NTP sync failed");
    }
}

void setup()
{
    Serial.begin(115200);

    // WiFi/NTP first, LCD/LVGL after -- avoids a known ESP32-S3 RGB-LCD-bounce-buffer
    // + WiFi flash-cache race (esp-arduino-libs/ESP32_Display_Panel#198). See
    // DEVICE_DATASHEET.md ("The WiFi + RGB LCD cache race") for the full writeup.
    Serial.println("Connecting to WiFi (before LCD init, to avoid RGB+WiFi cache race)...");
    connect_wifi_and_sync_time_headless();

    Serial.println("Initializing board");

    Board *board = new Board();
    board->init();
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating clock UI");
    lvgl_port_lock(-1);

    screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, screen_tap_cb, LV_EVENT_CLICKED, NULL);

    digital_group = build_digital_group(screen);
    analog_meter = build_analog_meter(screen);

    status_label = lv_label_create(screen);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(status_label, wifi_status_message);

    apply_mode_visibility();

    clock_timer = lv_timer_create(clock_update_cb, 1000, NULL);

    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    delay(1000);
}
