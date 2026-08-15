/**
 * Prayer Times + Clock Display for Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Forked from ClockDisplay (2026-08-07) to become a dedicated prayer-times app.
 * Tap anywhere to cycle: Prayer (screen 1, default/home) -> Analog (screen 2)
 * -> Digital (screen 3) -> back to Prayer.
 *
 * Screen 1 (prayer/home) layout: full-width title bar on top; below it, left
 * half shows a small analog clock + date + monospace digital time, right half
 * shows today's 7 prayer times (zone JHR02) with the next upcoming one in
 * yellow. v3 (planned): photo frame mode, also touch-selectable.
 *
 * Built on top of the ESP32_Display_Panel "simple_port" example, which handles
 * board bring-up (RGB LCD + CH422G IO expander + GT911 touch) and LVGL porting
 * for this exact board (see esp_panel_board_supported_conf.h ->
 * BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7).
 *
 * On boot: connects to WiFi and syncs time via NTP (local time = GMT+8, no DST)
 * *before* touching the LCD/LVGL -- see the comment in setup() for why (a known
 * ESP32-S3 RGB-LCD + WiFi cache-race crash, see DEVICE_DATASHEET.md #12).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "wifi_credentials.h"
#include "prayer_times_data.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// Local time is GMT+8, no daylight saving.
static const long GMT_OFFSET_SEC = 8 * 3600;
static const int DST_OFFSET_SEC = 0;
static const char *NTP_SERVER_1 = "pool.ntp.org";
static const char *NTP_SERVER_2 = "time.google.com";

static const int32_t SCREEN_W = 800;
static const int32_t SCREEN_H = 480;

// Per-mode screen background colors: deep/dark shades so white/yellow text
// stays readable. lv_color_make() takes 8-bit R,G,B.
static const lv_color_t BG_PRAYER = LV_COLOR_MAKE(0x00, 0x18, 0x08);  // very dark green
static const lv_color_t BG_ANALOG = LV_COLOR_MAKE(0x00, 0x00, 0x00);  // black -- a dark-but-not-black
                                                                       // bg made the meter's own black
                                                                       // dial background look like an
                                                                       // ugly circle floating on top
static const lv_color_t BG_DIGITAL = LV_COLOR_MAKE(0x2A, 0x00, 0x00); // very deep red

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

// Prayer times mode (screen 1, the default/home screen)
static lv_obj_t *prayer_group;
static lv_obj_t *prayer_date_label;
static lv_obj_t *prayer_no_data_label;
// Each visible prayer row is two labels: name + time (see VISIBLE_PRAYER_COLS
// below for which of the 7 PRAYER_MINUTES columns are actually shown).
// Sized/indexed by visible-row-slot (0..VISIBLE_PRAYER_COUNT-1), not by the
// raw PrayerColumn index.
static lv_obj_t *prayer_row_labels[PRAYER_TABLE_COLS];
static lv_obj_t *prayer_time_labels[PRAYER_TABLE_COLS];
// One highlight box per row, drawn behind the name+time labels, toggled
// visible with a yellow fill for whichever row is "next" (see
// update_prayer_display()).
static lv_obj_t *prayer_row_highlight[PRAYER_TABLE_COLS];
// Small home-screen analog clock + mono digital time, independent of the
// full-screen `analog_meter`/`digital_*` objects used by screens 2/3.
static lv_obj_t *home_meter;
static lv_meter_indicator_t *home_hand_hour;
static lv_meter_indicator_t *home_hand_min;
static lv_meter_indicator_t *home_hand_sec;
static lv_obj_t *home_mono_time_label;

enum DisplayMode {
    MODE_PRAYER = 0, // default/home screen
    MODE_ANALOG,
    MODE_DIGITAL,
    MODE_COUNT, // sentinel, keep last
};

static DisplayMode current_mode = MODE_PRAYER;
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

static void update_home_clock_display()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    int hour12 = timeinfo.tm_hour % 12;
    lv_meter_set_indicator_value(home_meter, home_hand_hour, hour12 * 5 + timeinfo.tm_min / 12);
    lv_meter_set_indicator_value(home_meter, home_hand_min, timeinfo.tm_min);
    lv_meter_set_indicator_value(home_meter, home_hand_sec, timeinfo.tm_sec);

    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);
    lv_label_set_text(home_mono_time_label, time_buf);
}

static const char *PRAYER_NAMES[PRAYER_TABLE_COLS] = {
    "Imsak", "Subuh", "Syuruk", "Zuhur", "Asar", "Maghrib", "Isyak",
};

// Imsak is left out of the on-screen list (not commonly displayed alongside
// the 5 daily prayers + syuruk) but its data stays in PRAYER_MINUTES -- this
// array just controls which columns get a row in the UI and in what order.
static const int VISIBLE_PRAYER_COLS[] = {
    PRAYER_SUBUH, PRAYER_SYURUK, PRAYER_ZUHUR, PRAYER_ASAR, PRAYER_MAGHRIB, PRAYER_ISYAK,
};
static const int VISIBLE_PRAYER_COUNT = sizeof(VISIBLE_PRAYER_COLS) / sizeof(VISIBLE_PRAYER_COLS[0]);

// PRAYER_MINUTES now covers the full year (365 rows, PRAYER_DOY[i] == i+1 for
// every row -- see prayer_times_data.h, generated from the official JAKIM
// e-Solat export). That makes this a direct index rather than a search, but it
// stays a lookup function (not inlined at each call site) and keeps its bounds
// check: PRAYER_TABLE_ROWS is 365, while tm_yday can reach day 366 in a leap
// year (2028, 2032, ...), which would otherwise read one row past the end of
// the table. Returns -1 for any out-of-range day so callers show "no data"
// instead of reading garbage or the wrong day.
static int find_prayer_row_for_doy(int doy)
{
    if (doy < 1 || doy > PRAYER_TABLE_ROWS) {
        return -1;
    }
    return doy - 1;
}

static void update_prayer_display()
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char date_buf[48];
    strftime(date_buf, sizeof(date_buf), "%A, %d %B %Y", &timeinfo);
    lv_label_set_text(prayer_date_label, date_buf);

    update_home_clock_display();

    int row = find_prayer_row_for_doy(timeinfo.tm_yday + 1); // tm_yday is 0-based
    if (row < 0) {
        lv_obj_clear_flag(prayer_no_data_label, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < VISIBLE_PRAYER_COUNT; i++) {
            lv_obj_add_flag(prayer_row_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(prayer_time_labels[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(prayer_row_highlight[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_obj_add_flag(prayer_no_data_label, LV_OBJ_FLAG_HIDDEN);

    int now_minutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    // Find the next *visible* prayer still to come today, if any, so it can be
    // highlighted -- Imsak is excluded from the UI, so it's excluded from this
    // search too (its data is still read from PRAYER_MINUTES below, just never
    // shown or highlighted).
    int next_slot = -1;
    for (int i = 0; i < VISIBLE_PRAYER_COUNT; i++) {
        if (PRAYER_MINUTES[row][VISIBLE_PRAYER_COLS[i]] > now_minutes) {
            next_slot = i;
            break;
        }
    }
    // Past Isyak (the last visible prayer of the day): nothing later remains
    // today, so wrap to tomorrow's Subuh -- always visible-slot 0, since Subuh
    // is first in VISIBLE_PRAYER_COLS regardless of which day it is.
    if (next_slot < 0) {
        next_slot = 0;
    }

    for (int i = 0; i < VISIBLE_PRAYER_COUNT; i++) {
        int c = VISIBLE_PRAYER_COLS[i];
        lv_obj_clear_flag(prayer_row_labels[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(prayer_time_labels[i], LV_OBJ_FLAG_HIDDEN);
        int mins = PRAYER_MINUTES[row][c];
        lv_label_set_text(prayer_row_labels[i], PRAYER_NAMES[c]);
        // 12-hour, no leading zero, no AM/PM (all prayer times of the day are
        // unambiguous from context, so it's implied rather than shown).
        int hour24 = mins / 60;
        int hour12 = hour24 % 12;
        if (hour12 == 0) {
            hour12 = 12;
        }
        char time_buf[8];
        snprintf(time_buf, sizeof(time_buf), "%d:%02d", hour12, mins % 60);
        lv_label_set_text(prayer_time_labels[i], time_buf);

        // Reset every tick, then apply the highlight only to the upcoming row --
        // "next" can move to a different row as time passes. Highlighted row
        // gets a yellow box behind it and deep green text; other rows are plain
        // white text on the (transparent, unboxed) background.
        bool is_next = (i == next_slot);
        lv_obj_set_style_bg_opa(prayer_row_highlight[i], is_next ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(prayer_row_highlight[i], lv_palette_main(LV_PALETTE_YELLOW), 0);
        lv_color_t text_color = is_next ? BG_PRAYER : lv_color_white();
        lv_obj_set_style_text_color(prayer_row_labels[i], text_color, 0);
        lv_obj_set_style_text_color(prayer_time_labels[i], text_color, 0);
    }
}

static void clock_update_cb(lv_timer_t *timer)
{
    if (!time_synced) {
        return;
    }
    switch (current_mode) {
        case MODE_ANALOG:
            update_analog_display();
            break;
        case MODE_DIGITAL:
            update_digital_display();
            break;
        case MODE_PRAYER:
        default:
            update_prayer_display();
            break;
    }
}

static void apply_mode_visibility()
{
    lv_obj_add_flag(digital_group, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(analog_meter, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(prayer_group, LV_OBJ_FLAG_HIDDEN);

    switch (current_mode) {
        case MODE_ANALOG:
            lv_obj_set_style_bg_color(screen, BG_ANALOG, 0);
            lv_obj_clear_flag(analog_meter, LV_OBJ_FLAG_HIDDEN);
            break;
        case MODE_DIGITAL:
            lv_obj_set_style_bg_color(screen, BG_DIGITAL, 0);
            lv_obj_clear_flag(digital_group, LV_OBJ_FLAG_HIDDEN);
            break;
        case MODE_PRAYER:
        default:
            lv_obj_set_style_bg_color(screen, BG_PRAYER, 0);
            lv_obj_clear_flag(prayer_group, LV_OBJ_FLAG_HIDDEN);
            break;
    }

    // Only render clock/date/prayer content once NTP has actually succeeded --
    // calling update_*_display() unconditionally here (as this used to do) could
    // run before connect_wifi_and_sync_time_headless() finishes syncing (e.g. on
    // a slow/marginal WiFi boot), reading whatever garbage time the RTC happens
    // to hold and showing a wrong date/time until the *next* successful update.
    // clock_timer's periodic tick (clock_update_cb) already re-renders every
    // second once time_synced flips true, so nothing else needs to poke this.
    if (time_synced) {
        switch (current_mode) {
            case MODE_ANALOG:
                update_analog_display();
                break;
            case MODE_DIGITAL:
                update_digital_display();
                break;
            case MODE_PRAYER:
            default:
                update_prayer_display();
                break;
        }
    }
}

static void screen_tap_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) {
        return;
    }
    current_mode = (DisplayMode)((current_mode + 1) % MODE_COUNT);
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

// Builds a clock-face meter widget (ticks + hour/min/sec needles) of the given
// diameter, parented and positioned by the caller afterward. Shared by the
// full-screen analog mode (screen 2) and the small home-screen clock in the
// prayer screen's left panel, so the tick/needle setup (and its fix, see NOTE
// below) only needs to exist once.
static lv_obj_t *build_clock_meter(lv_obj_t *parent, int32_t diam, lv_meter_indicator_t **out_hour,
                                    lv_meter_indicator_t **out_min, lv_meter_indicator_t **out_sec)
{
    lv_obj_t *meter = lv_meter_create(parent);
    lv_obj_set_size(meter, diam, diam);
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

    *out_sec = lv_meter_add_needle_line(meter, scale, 2, lv_palette_main(LV_PALETTE_RED), -20);
    *out_min = lv_meter_add_needle_line(meter, scale, 5, lv_color_white(), -30);
    *out_hour = lv_meter_add_needle_line(meter, scale, 8, lv_color_white(), -50);

    return meter;
}

static lv_obj_t *build_analog_meter(lv_obj_t *parent)
{
    lv_obj_t *meter = build_clock_meter(parent, SCREEN_H - 40, &hand_hour, &hand_min, &hand_sec);
    lv_obj_center(meter);
    return meter;
}

// Prayer/home screen layout:
//   +----------------------------------------------------------+
//   | Block 1: "Waktu Solat - Johor Bahru"  (full width, top)  |
//   +------------------------+-----------------------------------+
//   | Block 2 (left half):   | Block 3 (right half):              |
//   |  analog clock          |  7-row prayer times list,          |
//   |  + date                |  next prayer's row in yellow       |
//   |  + mono HH:MM:SS       |                                     |
//   +------------------------+-----------------------------------+
static const int32_t BLOCK1_H = 70;

static lv_obj_t *build_prayer_group(lv_obj_t *parent)
{
    lv_obj_t *group = lv_obj_create(parent);
    lv_obj_remove_style_all(group);
    lv_obj_set_size(group, SCREEN_W, SCREEN_H);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_CLICKABLE);

    // -- Block 1: full-width title bar --
    lv_obj_t *block1 = lv_obj_create(group);
    lv_obj_remove_style_all(block1);
    lv_obj_set_size(block1, SCREEN_W, BLOCK1_H);
    lv_obj_set_pos(block1, 0, 0);
    lv_obj_clear_flag(block1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(block1, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(block1);
    lv_label_set_text(title, "Waktu Solat - Johor Bahru");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_center(title);

    // -- Block 2 (left half): analog clock + date + mono digital time --
    int32_t block_y = BLOCK1_H;
    int32_t block_h = SCREEN_H - BLOCK1_H;
    int32_t block2_w = SCREEN_W / 2;

    lv_obj_t *block2 = lv_obj_create(group);
    lv_obj_remove_style_all(block2);
    lv_obj_set_size(block2, block2_w, block_h);
    lv_obj_set_pos(block2, 0, block_y);
    lv_obj_clear_flag(block2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(block2, LV_OBJ_FLAG_CLICKABLE);

    int32_t meter_diam = block_h - 90; // leave room for date above + mono time below
    home_meter = build_clock_meter(block2, meter_diam, &home_hand_hour, &home_hand_min, &home_hand_sec);
    // Nudged up ~half the digital-readout font size (montserrat_28 -> 14px) to
    // close the gap that was visible above the dial, since below it the dial
    // was already touching the mono time readout with no gap at all.
    lv_obj_align(home_meter, LV_ALIGN_TOP_MID, 0, 36);

    prayer_date_label = lv_label_create(block2);
    lv_label_set_text(prayer_date_label, "");
    lv_obj_set_style_text_color(prayer_date_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(prayer_date_label, &lv_font_montserrat_20, 0);
    lv_obj_set_width(prayer_date_label, block2_w);
    lv_obj_set_style_text_align(prayer_date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(prayer_date_label, LV_ALIGN_TOP_MID, 0, 5);

    home_mono_time_label = lv_label_create(block2);
    lv_label_set_text(home_mono_time_label, "--:--:--");
    lv_obj_set_style_text_color(home_mono_time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(home_mono_time_label, &lv_font_montserrat_28, 0);
    lv_obj_set_width(home_mono_time_label, block2_w);
    lv_obj_set_style_text_align(home_mono_time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(home_mono_time_label, LV_ALIGN_BOTTOM_MID, 0, -15);

    // -- Block 3 (right half): prayer times list --
    lv_obj_t *block3 = lv_obj_create(group);
    lv_obj_remove_style_all(block3);
    lv_obj_set_size(block3, SCREEN_W - block2_w, block_h);
    lv_obj_set_pos(block3, block2_w, block_y);
    lv_obj_clear_flag(block3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(block3, LV_OBJ_FLAG_CLICKABLE);

    // Each visible row (see VISIBLE_PRAYER_COLS -- Imsak is excluded) is a
    // highlight box (hidden unless this row is "next") behind two labels:
    // name and time, both in proportional Montserrat. Time sits at a fixed x,
    // giving a column that's visually aligned enough at this size/format
    // (12-hour, no leading zero) without needing a monospace font.
    static const int32_t TIME_COL_X = 260 - 38;
    static const int32_t ROW_BOX_RIGHT_MARGIN = 30; // clearance past the time text
    int32_t row_h = block_h / VISIBLE_PRAYER_COUNT;
    for (int i = 0; i < VISIBLE_PRAYER_COUNT; i++) {
        lv_obj_t *box = lv_obj_create(block3);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, (SCREEN_W - block2_w) - 40 - ROW_BOX_RIGHT_MARGIN, row_h - 4);
        lv_obj_set_pos(box, 30, i * row_h + 2);
        lv_obj_set_style_radius(box, 8, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0); // invisible unless "next"
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
        prayer_row_highlight[i] = box;

        lv_obj_t *name = lv_label_create(block3);
        lv_label_set_text(name, "");
        lv_obj_set_style_text_color(name, lv_color_white(), 0);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0);
        lv_obj_set_pos(name, 40, i * row_h + (row_h - 28) / 2);
        prayer_row_labels[i] = name;

        lv_obj_t *time_lbl = lv_label_create(block3);
        lv_label_set_text(time_lbl, "");
        lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_pos(time_lbl, TIME_COL_X, i * row_h + (row_h - 28) / 2);
        prayer_time_labels[i] = time_lbl;
    }

    prayer_no_data_label = lv_label_create(block3);
    lv_label_set_text(prayer_no_data_label, "No prayer time\ndata for today");
    lv_obj_set_style_text_color(prayer_no_data_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(prayer_no_data_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(prayer_no_data_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(prayer_no_data_label);
    lv_obj_add_flag(prayer_no_data_label, LV_OBJ_FLAG_HIDDEN);

    return group;
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
    // DEVICE_DATASHEET.md section 12 for the full writeup.
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
    // Background color is set per-mode in apply_mode_visibility() (see BG_PRAYER
    // / BG_ANALOG / BG_DIGITAL above) -- no need to set an initial color here.
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, screen_tap_cb, LV_EVENT_CLICKED, NULL);

    digital_group = build_digital_group(screen);
    analog_meter = build_analog_meter(screen);
    prayer_group = build_prayer_group(screen);

    // Only created/shown when WiFi/NTP genuinely failed at boot (wifi_status_message
    // non-empty) -- otherwise this stayed empty-but-visible on top of every screen
    // permanently, which is confusing regardless of what it says. There's no retry
    // logic (sync is attempted once, before the LCD/LVGL exist -- see setup()
    // comment on ordering), so this reflects the final boot-time outcome for the
    // rest of the session.
    if (wifi_status_message[0] != '\0') {
        status_label = lv_label_create(screen);
        lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(status_label, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(status_label, 6, 0);
        lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);
        lv_label_set_text(status_label, wifi_status_message);
        // Sits above all 3 mode groups/meter so it stays visible regardless of
        // which mode is active or gets shown/hidden later.
        lv_obj_move_foreground(status_label);
    }

    apply_mode_visibility();

    clock_timer = lv_timer_create(clock_update_cb, 1000, NULL);

    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    delay(1000);
}
