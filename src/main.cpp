/**
 * AI Usage Monitor - ESP32-2432S028R (CYD 2.8")
 * v2.0.0 — USB-Serial Only (no WiFi, no WebServer)
 *
 * Boot sequence:
 * 1. Init Serial (USB)
 * 2. Init display + touch + LVGL
 * 3. Show boot screen
 * 4. Init serial receiver
 * 5. Enter dashboard directly
 *
 * Data flow:
 *   Mac (CodexBar) --USB-Serial--> ESP32 --> Dashboard
 *
 * Navigation:
 *   Dashboard (default) -> long press (>1s) -> Settings screen
 *   Settings -> tap back arrow -> Dashboard
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Preferences.h>
#include "board.h"
#include "config.h"
#include "config_store.h"
#include "localization.h"
#include "serial_receiver.h"
#include "ui_common.h"
#include "ui_dashboard.h"
#include "ui_echodesk.h"
#include "ui_settings.h"
#include "wifi_time.h"

// ============================================================
// Globals
// ============================================================

// Runtime screen dimensions (defined here, declared extern in config.h)
uint16_t SCREEN_WIDTH  = DISPLAY_SHORT_SIDE;   // Default: Portrait 240
uint16_t SCREEN_HEIGHT = DISPLAY_LONG_SIDE;     // Default: Portrait 320

// LVGL display and input device
static lv_display_t  *lv_disp = nullptr;
static lv_indev_t    *lv_touch = nullptr;

// Dashboard state
static bool dashboard_active = false;
static unsigned long last_ui_update = 0;
static const unsigned long UI_UPDATE_INTERVAL = 1000;  // 1s interval for live countdown + clock

// Heap monitoring
static unsigned long lastHeapLog = 0;

// Loop debug counter
static unsigned long loopCount = 0;
static unsigned long lastLoopLog = 0;

// ============================================================
// Backlight control (LEDC PWM on PIN_TFT_BL)
// ============================================================
void backlight_apply_percent(uint8_t pct)
{
    board_backlight_set_percent(pct);
}

// ============================================================
// Orientation change WITHOUT reboot
//   - switches TFT rotation
//   - touch coordinate mapping follows the current orientation
//   - resizes LVGL display
//   - recreates dashboard so layout uses new SCREEN_WIDTH/HEIGHT
// ============================================================
// Lesbarer Name der Orientierung — nur fuer Logging.
static const char *orientation_name(uint8_t orientation)
{
    switch (orientation) {
        case ORIENTATION_LANDSCAPE_LEFT:  return "landscape_left";
        case ORIENTATION_LANDSCAPE_RIGHT: return "landscape_right";
        default:                          return "portrait";
    }
}

void apply_orientation(uint8_t orientation)
{
    board_set_rotation(orientation);

    // Blank the panel so no garbled pixels leak through during recreate
    board_fill_black();

    // Tell LVGL the new resolution — it reallocates the rendering state and
    // clips subsequent draws to the new extent.
    if (lv_disp) {
        lv_display_set_resolution(lv_disp, SCREEN_WIDTH, SCREEN_HEIGHT);
        // Boards ohne Hardware-Drehung (S3) drehen ueber LVGL; die Touch-
        // Koordinaten dreht LVGL dabei selbst mit.
        lv_display_set_rotation(lv_disp, board_lvgl_rotation());
    }

    // C4: The dashboard recreate is deferred to loop() (see serial_receiver).
    // All state above (rotation, SCREEN_WIDTH/HEIGHT, LVGL
    // resolution) is applied synchronously here; only the expensive full-screen
    // rebuild is taken out of the Serial RX path. apply_orientation() is only
    // ever called from the command parser, so deferring is safe.
    serial_request_ui_rebuild();

    Serial.printf("[TFT] Rotation switched live to %s (%ux%u)\n",
                  orientation_name(orientation), SCREEN_WIDTH, SCREEN_HEIGHT);
}

// ============================================================
// LVGL flush callback — geht ueber die Board-Schnittstelle
// ============================================================
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    board_flush(area, px_map);
    lv_display_flush_ready(disp);
}

// ============================================================
// LVGL touch read callback
// ============================================================
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static bool was_touched = false;
    uint16_t x = 0, y = 0;
    bool touched = board_touch_read(&x, &y);

    if (touched) {
        if (!was_touched) Serial.printf("[Touch] Press at %u,%u\n", x, y);
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        if (was_touched) Serial.println("[Touch] Release");
        data->state = LV_INDEV_STATE_RELEASED;
    }
    was_touched = touched;
}

// ============================================================
// Boot screen — LVGL, damit er auf beiden Boards gleich aussieht
// ============================================================
static lv_obj_t *boot_screen = nullptr;
static lv_obj_t *boot_status = nullptr;

#if defined(BOARD_FREENOVE_FNK0114A)
static lv_obj_t *boot_corner_tl = nullptr;
static lv_obj_t *boot_corner_br = nullptr;
static unsigned long boot_screen_started = 0;

static constexpr uint32_t BOOT_SCREEN_MIN_MS = 2000;
static constexpr int16_t BOOT_CORNER_W = 96;
static constexpr int16_t BOOT_CORNER_H = 68;
static constexpr int16_t BOOT_CORNER_THICKNESS = 4;
static constexpr int16_t BOOT_CORNER_MARGIN_X = 18;
static constexpr int16_t BOOT_CORNER_MARGIN_Y = 16;
static constexpr int16_t BOOT_CORNER_TRAVEL = 8;

static lv_obj_t *boot_create_rect(lv_obj_t *parent, int16_t width, int16_t height,
                                  int16_t x, int16_t y)
{
    lv_obj_t *rect = lv_obj_create(parent);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, width, height);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_color(rect, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, LV_PART_MAIN);
    return rect;
}

static lv_obj_t *boot_create_corner(lv_obj_t *parent, bool bottom_right,
                                    int16_t x, int16_t y)
{
    lv_obj_t *corner = lv_obj_create(parent);
    lv_obj_remove_style_all(corner);
    lv_obj_set_size(corner, BOOT_CORNER_W, BOOT_CORNER_H);
    lv_obj_set_pos(corner, x, y);

    if (bottom_right) {
        boot_create_rect(corner, BOOT_CORNER_W, BOOT_CORNER_THICKNESS,
                         0, BOOT_CORNER_H - BOOT_CORNER_THICKNESS);
        boot_create_rect(corner, BOOT_CORNER_THICKNESS, BOOT_CORNER_H,
                         BOOT_CORNER_W - BOOT_CORNER_THICKNESS, 0);
    } else {
        boot_create_rect(corner, BOOT_CORNER_W, BOOT_CORNER_THICKNESS, 0, 0);
        boot_create_rect(corner, BOOT_CORNER_THICKNESS, BOOT_CORNER_H, 0, 0);
    }
    return corner;
}

static lv_obj_t *boot_create_logo_label(const char *text, lv_color_t color,
                                        int16_t x, int16_t y, int16_t letter_space)
{
    lv_obj_t *label = lv_label_create(boot_screen);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, letter_space, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
    return label;
}

static void boot_anim_set_x(void *obj, int32_t value)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(obj), value);
}

static void boot_anim_set_y(void *obj, int32_t value)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), value);
}

static void boot_start_corner_animation(lv_obj_t *corner,
                                        int16_t start_x, int16_t end_x,
                                        int16_t start_y, int16_t end_y)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, corner);
    lv_anim_set_exec_cb(&anim, boot_anim_set_x);
    lv_anim_set_values(&anim, start_x, end_x);
    lv_anim_set_duration(&anim, 700);
    lv_anim_set_reverse_duration(&anim, 700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);

    lv_anim_init(&anim);
    lv_anim_set_var(&anim, corner);
    lv_anim_set_exec_cb(&anim, boot_anim_set_y);
    lv_anim_set_values(&anim, start_y, end_y);
    lv_anim_set_duration(&anim, 700);
    lv_anim_set_reverse_duration(&anim, 700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
}

static void draw_freenove_boot_screen(void)
{
    boot_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(boot_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(boot_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(boot_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int16_t br_x = static_cast<int16_t>(SCREEN_WIDTH) - BOOT_CORNER_W - BOOT_CORNER_MARGIN_X;
    const int16_t br_y = static_cast<int16_t>(SCREEN_HEIGHT) - BOOT_CORNER_H - BOOT_CORNER_MARGIN_Y;

    boot_corner_tl = boot_create_corner(boot_screen, false,
                                        BOOT_CORNER_MARGIN_X, BOOT_CORNER_MARGIN_Y);
    boot_corner_br = boot_create_corner(boot_screen, true, br_x, br_y);

    // Build an outlined word without a bitmap or custom font: eight white
    // copies form the stroke and the centered black copy cuts out the fill.
    static const int8_t outline_offsets[][2] = {
        {-2, -2}, {0, -2}, {2, -2},
        {-2,  0},           {2,  0},
        {-2,  2}, {0,  2}, {2,  2},
    };
    for (const auto &offset : outline_offsets) {
        boot_create_logo_label("ECHO", lv_color_white(), offset[0], -30 + offset[1], -1);
    }
    boot_create_logo_label("ECHO", lv_color_black(), 0, -30, -1);

    // Slightly layered copies give the built-in Montserrat face the same
    // heavy visual weight as the reference's DESK wordmark.
    boot_create_logo_label("DESK", lv_color_white(), -1, 27, 1);
    boot_create_logo_label("DESK", lv_color_white(),  0, 27, 1);
    boot_create_logo_label("DESK", lv_color_white(),  1, 27, 1);

    boot_start_corner_animation(
        boot_corner_tl,
        BOOT_CORNER_MARGIN_X, BOOT_CORNER_MARGIN_X + BOOT_CORNER_TRAVEL,
        BOOT_CORNER_MARGIN_Y, BOOT_CORNER_MARGIN_Y + BOOT_CORNER_TRAVEL);
    boot_start_corner_animation(
        boot_corner_br,
        br_x, br_x - BOOT_CORNER_TRAVEL,
        br_y, br_y - BOOT_CORNER_TRAVEL);

    boot_screen_started = millis();
    lv_screen_load(boot_screen);
    lv_refr_now(NULL);
}
#endif

static void draw_boot_screen(void)
{
#if defined(BOARD_FREENOVE_FNK0114A)
    draw_freenove_boot_screen();
#else
    boot_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(boot_screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(boot_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(boot_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(boot_screen);
    lv_label_set_text(title, APP_NAME);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    boot_status = lv_label_create(boot_screen);
    lv_label_set_text(boot_status, L(STR_INITIALIZING));
    lv_obj_set_style_text_color(boot_status, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_text_font(boot_status, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(boot_status, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *version = lv_label_create(boot_screen);
    lv_label_set_text(version, "v" APP_VERSION);
    lv_obj_set_style_text_color(version, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(version, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    lv_screen_load(boot_screen);
    lv_refr_now(NULL);
#endif
}

static void wait_for_boot_screen(void)
{
#if defined(BOARD_FREENOVE_FNK0114A)
    while (millis() - boot_screen_started < BOOT_SCREEN_MIN_MS) {
        lv_timer_handler();
        delay(5);
    }
#else
    delay(1000);
#endif
}

// ============================================================
// Update boot status text on display
// ============================================================
static void update_boot_status(const char *msg) {
    if (boot_status != nullptr) {
        lv_label_set_text(boot_status, msg);
        lv_refr_now(NULL);
    }
    Serial.printf("[Boot] %s\n", msg);
}

// ============================================================
// Transition to main dashboard UI
// ============================================================
static void enter_main_ui(void) {
#if defined(ECHODESK_OS)
    ui_echodesk_init();
    dashboard_active = false;
    boot_screen = nullptr;
    boot_corner_tl = nullptr;
    boot_corner_br = nullptr;
    Serial.println("[UI] EchoDesk OS Home active (USB receiver remains online)");
#else
    ui_dashboard_create();
    lv_obj_t *dash_scr = ui_dashboard_get_screen();
    if (dash_scr) {
#if defined(BOARD_FREENOVE_FNK0114A)
        lv_screen_load_anim(dash_scr, LV_SCR_LOAD_ANIM_FADE_IN, 250, 0, true);
        boot_screen = nullptr;
        boot_corner_tl = nullptr;
        boot_corner_br = nullptr;
#else
        lv_screen_load(dash_scr);
#endif
    }
    dashboard_active = true;

    MonitorState initState = serial_get_state();
    ui_dashboard_update(initState);

    Serial.println("[UI] Dashboard active (waiting for USB data)");
    lv_refr_now(NULL);
#endif
}

// ============================================================
// Setup
// ============================================================
void setup()
{
    // --- Serial init (BEFORE begin for RX buffer) ---
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    delay(500);
    Serial.println("========================================");
    Serial.printf("%s v%s (USB-Serial)\n", APP_NAME, APP_VERSION);
    Serial.println("========================================");

    // --- Display + Touch ueber die Board-Schnittstelle ---
    // Muss vor dem Backlight laufen: die SPI-Boards bringen dabei ihre
    // Panel-Reset-Sequenz hoch, das RGB-Board legt den Framebuffer an.
    board_display_init();
    board_backlight_init();

    // Einstellungen laden. Auf Boards ohne dauerhafte Ablage sind das die
    // Voreinstellungen, bis der Host sein Geraeteprofil schickt.
    config_load(g_config);
#if defined(ECHODESK_OS)
    // EchoDesk is designed specifically for the 320x240 landscape panel.
    // Preserve either landscape cable orientation, but recover old/corrupt
    // NVS values before LVGL allocates its display. Its local brightness UI
    // intentionally exposes 10..100 percent, so normalize that range too.
    bool echodesk_config_changed = false;
    if (g_config.orientation != ORIENTATION_LANDSCAPE_LEFT &&
        g_config.orientation != ORIENTATION_LANDSCAPE_RIGHT) {
        g_config.orientation = ORIENTATION_LANDSCAPE_RIGHT;
        echodesk_config_changed = true;
        Serial.println("[EchoDesk] Portrait setting normalized to landscape_right");
    }
    if (g_config.brightness_pct < 10) {
        g_config.brightness_pct = 10;
        echodesk_config_changed = true;
        Serial.println("[EchoDesk] Brightness normalized to 10% minimum");
    }
    if (echodesk_config_changed) config_save(g_config);
#endif
    backlight_apply_percent(g_config.brightness_pct);

    // Apply persisted theme before creating UI
    ui_apply_theme(g_config.theme);

    // Drehung und SCREEN_WIDTH/HEIGHT setzen, bevor LVGL das Display anlegt.
    board_set_rotation(g_config.orientation);
    board_fill_black();
    Serial.printf("[Display] %s initialisiert (%ux%u)\n",
                  board_display_id(), SCREEN_WIDTH, SCREEN_HEIGHT);

    // --- LVGL init ---
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
    Serial.println("[LVGL] Core initialized + tick callback registered");

    void *lv_buf1 = nullptr, *lv_buf2 = nullptr;
    uint32_t lv_buf_bytes = 0;
    board_lvgl_buffers(&lv_buf1, &lv_buf2, &lv_buf_bytes);

    lv_disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_rotation(lv_disp, board_lvgl_rotation());
    lv_display_set_flush_cb(lv_disp, disp_flush_cb);
    lv_display_set_buffers(lv_disp, lv_buf1, lv_buf2,
                           lv_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    Serial.printf("[LVGL] Display driver registered (%ux%u)\n", SCREEN_WIDTH, SCREEN_HEIGHT);

    lv_touch = lv_indev_create();
    lv_indev_set_type(lv_touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lv_touch, touch_read_cb);
    Serial.println("[LVGL] Touch input registered");

    // --- Boot screen ---
    draw_boot_screen();

    // --- Serial receiver init ---
    serial_receiver_init();
    wifi_time_init();
    update_boot_status(L(STR_USB_CONNECTED));
    wait_for_boot_screen();

    // --- Enter dashboard ---
    enter_main_ui();

    // --- Heap info ---
    Serial.printf("[System] Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("[System] Min free heap: %u bytes\n", ESP.getMinFreeHeap());
    Serial.println("========================================");
    Serial.println("Setup complete. Entering main loop.");
}

// ============================================================
// Main loop
// ============================================================
void loop()
{
    lv_timer_handler();

    // Read serial data
    serial_receiver_tick();
    wifi_time_tick();

    // C4: Perform any UI rebuild requested by the command parser here, OUTSIDE
    // the Serial RX loop, so the expensive full-screen recreate is not reentrant
    // with frame reception. State (theme/language/orientation/dimensions) was
    // already applied synchronously in the parser.
    if (serial_ui_rebuild_requested()) {
#if defined(ECHODESK_OS)
        ui_echodesk_recreate();
#else
        ui_dashboard_recreate();
#endif
        serial_ui_rebuild_clear();
    }

#if defined(ECHODESK_OS)
    ui_echodesk_tick();
#else
    // Update dashboard UI on new data OR every 1s (clock/countdown)
    if (dashboard_active) {
        if (serial_has_new_data() || (millis() - last_ui_update >= UI_UPDATE_INTERVAL)) {
            MonitorState curState = serial_get_state();
            ui_dashboard_update(curState);
            last_ui_update = millis();
        }
    }
#endif

    // Loop debug + heap monitoring (every 10 seconds)
    loopCount++;
    if (millis() - lastLoopLog >= 10000) {
        unsigned long elapsed = millis() - lastLoopLog;
        Serial.printf("[Loop] %lu iterations in %lu ms | tick=%u | heap=%u\n",
                      loopCount, elapsed, (unsigned)lv_tick_get(), ESP.getFreeHeap());
        loopCount = 0;
        lastLoopLog = millis();
    }

    // Periodic heap monitoring (every 60 seconds)
    if (millis() - lastHeapLog > 60000) {
        lastHeapLog = millis();
        Serial.printf("[Heap] Free: %u  Min: %u\n",
                      ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }

    delay(5);
}
