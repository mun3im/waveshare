/**
 * Bouncing Ball + Network Clock demo for Waveshare ESP32-S3-Touch-LCD-7 (800x480)
 *
 * Built on top of the ESP32_Display_Panel "simple_port" example, which handles
 * board bring-up (RGB LCD + CH422G IO expander + GT911 touch) and LVGL porting
 * for this exact board (see esp_panel_board_supported_conf.h ->
 * BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7).
 *
 * On boot: connects to WiFi, fetches time via NTP (local time = GMT+8, no DST),
 * then shows a red ball bouncing around the screen with a live clock label
 * riding inside it.
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

static lv_obj_t *ball;
static lv_obj_t *time_label;
static lv_obj_t *status_label;
static lv_timer_t *anim_timer;
static lv_timer_t *clock_timer;

// Ball state (in LVGL display coordinates)
static const int32_t BALL_DIAM = 140;
static int32_t screen_w = 800;
static int32_t screen_h = 480;
static float pos_x, pos_y;
static float vel_x = 4.5f, vel_y = 3.2f;

static bool time_synced = false;
static char wifi_status_message[64] = "";

static void ball_move_cb(lv_timer_t *timer)
{
    pos_x += vel_x;
    pos_y += vel_y;

    if (pos_x <= 0) {
        pos_x = 0;
        vel_x = -vel_x;
    } else if (pos_x >= screen_w - BALL_DIAM) {
        pos_x = screen_w - BALL_DIAM;
        vel_x = -vel_x;
    }

    if (pos_y <= 0) {
        pos_y = 0;
        vel_y = -vel_y;
    } else if (pos_y >= screen_h - BALL_DIAM) {
        pos_y = screen_h - BALL_DIAM;
        vel_y = -vel_y;
    }

    lv_obj_set_pos(ball, (int32_t)pos_x, (int32_t)pos_y);
}

static void clock_update_cb(lv_timer_t *timer)
{
    if (!time_synced) {
        return;
    }
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
    lv_label_set_text(time_label, buf);
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

    // Connect to WiFi and sync NTP time *before* touching the RGB LCD panel/LVGL.
    // The ESP32-S3's RGB LCD driver uses a bounce-buffer refresh ISR that reads
    // cached PSRAM; WiFi.begin() briefly disables the flash cache while it does
    // its own flash/NVS access. If both run concurrently, the bounce-buffer ISR
    // can fire mid-cache-disable and crash with "Cache disabled but cached memory
    // region accessed" (a known esp-arduino-libs/ESP32_Display_Panel issue, #198).
    // Doing the network step first, before the LCD/LVGL timers exist, avoids the
    // race entirely.
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

    screen_w = 800;
    screen_h = 480;
    pos_x = screen_w / 2.0f - BALL_DIAM / 2.0f;
    pos_y = screen_h / 2.0f - BALL_DIAM / 2.0f;

    Serial.println("Creating bouncing ball UI");
    lvgl_port_lock(-1);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    ball = lv_obj_create(scr);
    lv_obj_set_size(ball, BALL_DIAM, BALL_DIAM);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ball, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ball, 0, 0);
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ball, (int32_t)pos_x, (int32_t)pos_y);

    // Clock label centered inside the ball, rides along with it.
    time_label = lv_label_create(ball);
    lv_label_set_text(time_label, time_synced ? "--:--:--" : "no time");
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_20, 0);
    lv_obj_center(time_label);

    // Status label shows any WiFi/NTP failure; blank on success.
    status_label = lv_label_create(scr);
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(status_label, wifi_status_message);

    // Advance the ball every 16ms (~60 FPS)
    anim_timer = lv_timer_create(ball_move_cb, 16, NULL);
    // Refresh the clock label once a second
    clock_timer = lv_timer_create(clock_update_cb, 1000, NULL);

    lvgl_port_unlock();

    Serial.println("Setup done");
}

void loop()
{
    delay(1000);
}
