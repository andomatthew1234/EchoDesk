/**
 * Serial Receiver - USB-Serial JSON data receiver
 *
 * Reads single-line JSON from Serial (Mac sends via USB).
 * Parses usage data and updates MonitorState.
 * Sets ESP32 system clock from the "time" field.
 *
 * Expected JSON format (legacy, one line, terminated with \n):
 * {"time":"2026-04-12T15:30:00Z","data":[{"source":"oauth","usage":{...},"provider":"claude"}]}
 *
 * Framed format (v2.12.3+):
 * AIM1 <byteLength> <frameId>\n
 * <JSON payload bytes>
 */

#include "serial_receiver.h"
#include "api_common.h"
#include "board.h"
#include "config.h"
#include "providers.h"
#include "plugin_scene.h"
#if defined(ECHODESK_OS)
#include "echodesk_control.h"
#include "echodesk_setup.h"
#include "performance_data.h"
#include "music_data.h"
#include "now_playing_layout.h"
#include "ui_echodesk.h"
#include "ui_performance.h"
#endif
#include "config_store.h"
#include "localization.h"
#include "ui_common.h"
#include "ui_dashboard.h"
#include "wifi_time.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

// ============================================================
// Constants
// ============================================================
static const size_t SERIAL_BUF_SIZE = 4096;
static const size_t SERIAL_FRAME_MAX_SIZE = SERIAL_BUF_SIZE - 1;
static const char *SERIAL_FRAME_MAGIC = "AIM1";
static const unsigned long DATA_TIMEOUT_MS = 300000;  // 5 minutes
static const uint8_t USAGE_ROW_MAX = 3;
// Fenstergroesse (Minuten) ab der ein Usage-Feld als "weekly" gilt: 7*24*60.
static const int WEEKLY_WINDOW_MINUTES = 10080;
// C1: Wird ein framed-Payload nach dem Header nicht innerhalb dieser Zeit
// vollstaendig empfangen, gilt der Frame als abgebrochen und wird verworfen.
static const unsigned long SERIAL_FRAME_TIMEOUT_MS = 2000;
// Schema 2 adds bounded plugin scene frames; schema 1 usage frames stay valid.
static const int SUPPORTED_SCHEMA_VERSION = 2;

// ============================================================
// State
// ============================================================
static char serial_buf[SERIAL_BUF_SIZE];
static size_t serial_buf_pos = 0;
static bool serial_discard_until_newline = false;
static bool serial_receiving_frame = false;
static size_t serial_frame_expected = 0;
static size_t serial_frame_received = 0;
// C1: Wann der aktuelle framed-Payload begonnen hat (millis()), fuer den
// Resync-Timeout falls die Uebertragung mitten im Payload abbricht.
static unsigned long serial_frame_started_ms = 0;
// Kennung aus dem Header des laufenden Frames, fuer Fehlermeldungen beim Abbruch.
static long serial_frame_id = -1;

static MonitorState state;
static const uint8_t VIEW_MAX = 8;
// Außerhalb der Provider-IDs, damit ein neuer Provider nicht mit der Uhr kollidiert.
static const uint8_t VIEW_CLOCK = 0xFF;
static const uint8_t VIEW_PLUGIN = 0xFE;
static MonitorState view_states[VIEW_MAX];
static uint8_t view_types[VIEW_MAX] = {PROVIDER_CLAUDE};
static char view_keys[VIEW_MAX][PLUGIN_VIEW_KEY_BYTES] = {};
static uint8_t view_count = 1;
static uint8_t active_view = 0;
static bool views_automatic = false;
static uint16_t view_interval_seconds = 10;
static unsigned long last_view_change = 0;
static unsigned long last_host_frame_ms = 0;
static bool host_frame_seen = false;
// Erst ein Host mit set_views schaltet Wechsel per Timer und Touch frei. Ohne
// ihn (Boot, alter Companion) bleibt die gespeicherte Auswahl stehen.
static bool views_host_configured = false;
static bool new_data_flag = false;
static char display_time[6] = "--:--";
static int16_t timezone_offset_minutes = 60;  // Default: Europe/Berlin winter time

// C4: UI rebuilds are no longer triggered synchronously from the parser
// (reentrant full-screen recreate mid-RX). Commands set this flag and the
// rebuild happens in main loop() after serial_receiver_tick().
static bool ui_rebuild_pending = false;

// C3: Set once the Mac companion sends a TZ offset. While set, wifi_time must
// not overwrite the TZ env var with its fixed NTP timezone — only sync UTC.
static bool host_tz_set = false;

#if defined(ECHODESK_OS)
static uint32_t echodesk_settings_revision = 1;
static bool echodesk_state_dirty = false;
#endif

static void print_view_state();
static void persist_touch_view();

uint8_t serial_view_count() { return view_count; }
bool serial_is_clock_view() { return view_types[active_view] == VIEW_CLOCK; }
bool serial_is_plugin_view() { return view_types[active_view] == VIEW_PLUGIN; }
uint8_t serial_active_view() { return active_view; }

void serial_select_view(uint8_t index) {
    if (index >= view_count) return;
    active_view = index;
    state = view_states[index];
    last_view_change = millis();
    new_data_flag = true;
    Serial.printf("[Views] Active %u/%u (%s)\n", (unsigned)(index + 1),
                  (unsigned)view_count, serial_is_clock_view() ? "clock" :
                  serial_is_plugin_view() ? view_keys[index] : state.provider_label);
}

void serial_next_view() {
    if (views_host_configured && view_count > 1) {
        serial_select_view((active_view + 1) % view_count);
        persist_touch_view();
    }
}

void serial_previous_view() {
    if (views_host_configured && view_count > 1) {
        serial_select_view((active_view + view_count - 1) % view_count);
        persist_touch_view();
    }
}

void serial_request_ui_rebuild() {
    ui_rebuild_pending = true;
}

void serial_echodesk_state_changed() {
#if defined(ECHODESK_OS)
    ++echodesk_settings_revision;
    if (echodesk_settings_revision == 0) echodesk_settings_revision = 1;
    echodesk_state_dirty = true;
#endif
}

// ============================================================
// Provider helpers
// ------------------------------------------------------------
// provider_from_string(), provider_label_from_id() and
// default_row_title_for_provider() now live in providers.cpp and
// read from the central PROVIDERS[] table.
// ============================================================
static void clear_usage_rows(UsageData &usage) {
    usage.row_count = 0;
    for (uint8_t i = 0; i < USAGE_ROW_MAX; i++) {
        usage.row_utilization[i] = 0.0f;
        usage.row_title[i][0] = '\0';
        usage.row_resets_at[i][0] = '\0';
        usage.row_reset_epoch[i] = 0;
        usage.row_window_minutes[i] = 0;
    }
}

static void set_usage_row(
    UsageData &usage,
    uint8_t index,
    uint8_t provider,
    const char *title,
    float percent,
    const char *resets_at,
    int window_minutes
) {
    if (index >= USAGE_ROW_MAX) return;

    usage.row_window_minutes[index] = window_minutes > 0 ? (uint32_t)window_minutes : 0;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;
    usage.row_utilization[index] = percent / 100.0f;

    const char *fallback_title = default_row_title_for_provider(provider, index);
    strlcpy(usage.row_title[index], (title && title[0] != '\0') ? title : fallback_title,
            sizeof(usage.row_title[index]));

    if (resets_at && resets_at[0] != '\0') {
        strlcpy(usage.row_resets_at[index], resets_at, sizeof(usage.row_resets_at[index]));
        usage.row_reset_epoch[index] = iso8601_to_epoch(resets_at);
    } else {
        usage.row_resets_at[index][0] = '\0';
        usage.row_reset_epoch[index] = 0;
    }

    if ((index + 1) > usage.row_count) {
        usage.row_count = index + 1;
    }
}

static void print_frame_ack(int frame_id, int schema_version, size_t bytes, uint8_t provider, uint8_t rows) {
    if (frame_id < 0) return;
    Serial.printf("{\"type\":\"ack\",\"frameId\":%d,\"schemaVersion\":%d,"
                  "\"message\":\"accepted\",\"bytes\":%u,\"provider\":\"%s\","
                  "\"rows\":%u,\"heap\":%u}\n",
                  frame_id,
                  schema_version,
                  (unsigned)bytes,
                  provider_label_from_id(provider),
                  (unsigned)rows,
                  (unsigned)ESP.getFreeHeap());
}

static void print_frame_error(int frame_id, int schema_version, const char *message) {
    if (frame_id < 0) return;
    Serial.printf("{\"type\":\"error\",\"frameId\":%d,\"schemaVersion\":%d,"
                  "\"message\":\"%s\"}\n",
                  frame_id,
                  schema_version,
                  message ? message : "unknown");
}

static bool begin_framed_payload_from_header(const char *line) {
    const size_t magic_len = strlen(SERIAL_FRAME_MAGIC);
    if (strncmp(line, SERIAL_FRAME_MAGIC, magic_len) != 0 ||
        line[magic_len] != ' ') {
        return false;
    }

    char *end = nullptr;
    unsigned long length = strtoul(line + magic_len + 1, &end, 10);
    while (end && *end == ' ') end++;

    long header_frame_id = -1;
    if (end && *end != '\0') {
        char *frame_end = nullptr;
        header_frame_id = strtol(end, &frame_end, 10);
        if (frame_end == end) header_frame_id = -1;
    }

    if (length == 0 || length > SERIAL_FRAME_MAX_SIZE) {
        Serial.printf("{\"type\":\"error\",\"frameId\":%ld,"
                      "\"message\":\"Invalid frame length %lu\"}\n",
                      header_frame_id,
                      length);
        return true;
    }

    serial_receiving_frame = true;
    serial_frame_expected = (size_t)length;
    serial_frame_received = 0;
    serial_frame_started_ms = millis();  // C1: start of resync timeout window
    serial_frame_id = header_frame_id;
    serial_buf_pos = 0;
    return true;
}

// ============================================================
// Getter: display time string sent by Mac companion app
// ============================================================
const char* serial_get_display_time() {
    return display_time;
}

static void apply_timezone_offset_minutes(int offset_minutes) {
    if (offset_minutes < -14 * 60 || offset_minutes > 14 * 60) {
        return;
    }

    timezone_offset_minutes = (int16_t)offset_minutes;

#if defined(ECHODESK_OS)
    // The host offset is still accepted for backwards-compatible telemetry,
    // but EchoDesk uses its persisted named timezone so DST keeps advancing
    // correctly between companion frames.
    serial_apply_configured_timezone();
    return;
#endif

    // POSIX TZ signs are inverted: UTC+2 is encoded as "AIM-2".
    int posix_minutes = -offset_minutes;
    char sign = '+';
    if (posix_minutes < 0) {
        sign = '-';
        posix_minutes = -posix_minutes;
    }

    int hours = posix_minutes / 60;
    int minutes = posix_minutes % 60;
    char tz_buf[16];
    if (minutes == 0) {
        snprintf(tz_buf, sizeof(tz_buf), "AIM%c%d", sign, hours);
    } else {
        snprintf(tz_buf, sizeof(tz_buf), "AIM%c%d:%02d", sign, hours, minutes);
    }

    setenv("TZ", tz_buf, 1);
    tzset();
}

void serial_apply_configured_timezone() {
#if defined(ECHODESK_OS)
    const char *posix_rule = "AEST-10AEDT,M10.1.0,M4.1.0/3";
    if (g_config.timezone == ECHODESK_TIMEZONE_UTC) {
        posix_rule = "UTC0";
    }
    // Melbourne and Sydney currently share the same Australian eastern DST
    // transition rule, but remain distinct public IANA settings on the wire.
    setenv("TZ", posix_rule, 1);
    tzset();
#endif
}

// ============================================================
// Helper: parse and execute serial commands
// Returns true if a command was handled (caller should skip
// usage-data parsing).
// ============================================================
// --- set_orientation (dynamic — no reboot) ---
static void handle_set_orientation(JsonDocument &doc) {
    const char *val = doc["value"];
    if (!val) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_orientation: missing value\"}");
        return;
    }
    uint8_t new_orient;
    const char *applied_val = val;
    if (strcmp(val, "portrait") == 0) {
        new_orient = ORIENTATION_PORTRAIT;
    } else if (strcmp(val, "landscape_left") == 0 || strcmp(val, "landscape") == 0) {
        new_orient = ORIENTATION_LANDSCAPE_LEFT;
    } else if (strcmp(val, "landscape_right") == 0) {
        new_orient = ORIENTATION_LANDSCAPE_RIGHT;
    } else {
        Serial.printf("{\"type\":\"error\",\"message\":\"set_orientation: invalid value '%s'\"}\n", val);
        return;
    }
#if defined(ECHODESK_OS)
    if (new_orient == ORIENTATION_PORTRAIT) {
        new_orient = ORIENTATION_LANDSCAPE_RIGHT;
        applied_val = "landscape_right";
    }
#endif
    if (new_orient != g_config.orientation) {
        g_config.orientation = new_orient;
        config_save(g_config);
        apply_orientation(new_orient);  // live rotation — no ESP.restart()
    }
    Serial.printf("{\"type\":\"ok\",\"cmd\":\"set_orientation\",\"value\":\"%s\"}\n", applied_val);
}

// --- set_brightness (0..100 percent) ---
// `persist=false` applies the PWM live without writing NVS. This lets the
// Mac app preview slider changes and save only the final debounced value.
static void handle_set_brightness(JsonDocument &doc) {
    if (!doc["value"].is<int>()) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_brightness: missing or invalid value\"}");
        return;
    }
    int val = doc["value"];
#if defined(ECHODESK_OS)
    if (val < 10) val = 10;
#else
    if (val < BRIGHTNESS_MIN_PERCENT) val = BRIGHTNESS_MIN_PERCENT;
#endif
    if (val > BRIGHTNESS_MAX_PERCENT) val = BRIGHTNESS_MAX_PERCENT;
    bool persist = doc["persist"] | true;
    const bool changed = g_config.brightness_pct != static_cast<uint8_t>(val);
    g_config.brightness_pct = (uint8_t)val;
    backlight_apply_percent(g_config.brightness_pct);
    if (persist) {
        config_save(g_config);
    }
#if defined(ECHODESK_OS)
    if (changed) serial_echodesk_state_changed();
#endif
    Serial.printf("{\"type\":\"ok\",\"cmd\":\"set_brightness\",\"value\":%d,\"persist\":%s}\n",
                  val,
                  persist ? "true" : "false");
}

// --- wifi_set ---
static void handle_wifi_set(JsonDocument &doc) {
    const char *ssid = doc["ssid"];
    const char *password = doc["password"] | "";
    if (ssid == nullptr || ssid[0] == '\0') {
        Serial.println("{\"type\":\"wifi_status\",\"configured\":false,\"connected\":false,\"ssid\":\"\",\"ip\":\"\",\"rssi\":0,\"timeSynced\":false,\"error\":\"missing ssid\"}");
        return;
    }
    wifi_time_save_credentials(ssid, password);
    wifi_time_connect_now(10000);
    wifi_time_print_status();
}

// --- wifi_forget ---
static void handle_wifi_forget() {
    wifi_time_forget_credentials();
    wifi_time_print_status();
}

// --- standby ---
// Sent by the Mac app during a clean quit. Cable pulls and sleep still use
// the normal timeout fallback.
static void handle_standby() {
    // The companion is leaving intentionally, so do not wait five minutes
    // for its last data frame to expire before showing the standby clock.
    host_frame_seen = false;
#if defined(ECHODESK_OS)
    performance_data_mark_disconnected();
    music_data_mark_disconnected();
#endif
    for (uint8_t i = 0; i < view_count; ++i) {
        if (view_states[i].usage.valid && view_states[i].usage.last_fetch > 0) {
            view_states[i].usage.last_fetch = millis() - DATA_TIMEOUT_MS - 1;
        }
        strlcpy(view_states[i].status, "Standby", sizeof(view_states[i].status));
    }
    state = view_states[active_view];
    new_data_flag = true;
    Serial.println("{\"type\":\"ok\",\"cmd\":\"standby\"}");
}

// --- set_theme ---
static void handle_set_theme(JsonDocument &doc) {
    const char *val = doc["value"];
    if (!val) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_theme: missing value\"}");
        return;
    }
    uint8_t new_theme;
    if (strcmp(val, "light") == 0) {
        new_theme = THEME_LIGHT;
    } else if (strcmp(val, "dark") == 0) {
        new_theme = THEME_DARK;
    } else {
        Serial.printf("{\"type\":\"error\",\"message\":\"set_theme: invalid value '%s'\"}\n", val);
        return;
    }
    const bool changed = g_config.theme != new_theme;
    g_config.theme = new_theme;
    config_save(g_config);
    ui_apply_theme(new_theme);
    // Recreate dashboard with new colors (deferred to main loop — see C4)
    serial_request_ui_rebuild();
#if defined(ECHODESK_OS)
    if (changed) serial_echodesk_state_changed();
#endif
    Serial.printf("{\"type\":\"ok\",\"cmd\":\"set_theme\",\"value\":\"%s\"}\n", val);
}

// --- set_language ---
static void handle_set_language(JsonDocument &doc) {
    const char *val = doc["value"];
    if (!val) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_language: missing value\"}");
        return;
    }
    uint8_t new_lang;
    if (strcmp(val, "de") == 0) {
        new_lang = LANG_DE;
    } else if (strcmp(val, "en") == 0) {
        new_lang = LANG_EN;
    } else {
        Serial.printf("{\"type\":\"error\",\"message\":\"set_language: invalid value '%s'\"}\n", val);
        return;
    }
    g_language = new_lang;
    g_config.language = new_lang;
    config_save(g_config);
    // Recreate dashboard with new language (deferred to main loop — see C4)
    serial_request_ui_rebuild();
    Serial.printf("{\"type\":\"ok\",\"cmd\":\"set_language\",\"value\":\"%s\"}\n", val);
}

// --- get_info ---
static void handle_get_info() {
    const char *orient;
    switch (g_config.orientation) {
        case ORIENTATION_LANDSCAPE_LEFT:  orient = "landscape_left";  break;
        case ORIENTATION_LANDSCAPE_RIGHT: orient = "landscape_right"; break;
        case ORIENTATION_PORTRAIT:
        default:                          orient = "portrait";        break;
    }
    const char *theme = (g_config.theme == THEME_LIGHT) ? "light" : "dark";
    const char *lang = (g_config.language == LANG_EN) ? "en" : "de";
    // MAC (Wi-Fi STA, lowercase-Hex mit Doppelpunkten) als Device-ID fuer
    // Per-Device-Profile in der Mac-App (ab App v1.14.0 / FW v2.10.0).
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#if defined(ECHODESK_OS)
    const char *echodesk_capabilities =
        "\"performanceProtocol\":1,\"musicProtocol\":1,"
        "\"musicBackgroundProtocol\":1,\"musicVisualizerProtocol\":1,";
    const char *echodesk_control_capability =
        "\"echoDeskControlProtocol\":1,\"echoDeskSetupProtocol\":1,"
        "\"echoDeskWifiSetupProtocol\":1,";
#else
    const char *echodesk_capabilities = "";
    const char *echodesk_control_capability = "";
#endif
    // `display` kommt aus DISPLAY_ID (config.h), Compile-Zeit-Ableitung
    // aus ILI9341_DRIVER / ST7789_DRIVER. Info-Only, keine FW-Logik.
    // Seit v2.10.1.
    // `panel` ist der beim Start aus dem Panel gelesene Controller, `panelId`
    // die Rohwerte dazu. Seit v2.23.0.
    Serial.printf("{\"type\":\"info\",\"version\":\"%s\",\"mac\":\"%s\","
                  "\"display\":\"%s\",\"panel\":\"%s\",\"panelId\":\"%s\","
                  "\"orientation\":\"%s\","
                  "\"theme\":\"%s\",\"language\":\"%s\",\"brightness\":%u,"
                  "\"serialTransport\":\"%s\",\"maxFrameBytes\":%u,%s%s\"sceneProtocol\":1,"
                  "\"wifiConfigured\":%s,\"wifiConnected\":%s,\"timeSynced\":%s,"
                  "\"uptime\":%lu,\"heap\":%u,\"frameGlitches\":%lu}\n",
                  APP_VERSION, mac_str, DISPLAY_ID, board_panel_id(), board_panel_raw(),
                  orient, theme, lang,
                  (unsigned)g_config.brightness_pct,
                  SERIAL_FRAME_MAGIC,
                  (unsigned)SERIAL_FRAME_MAX_SIZE,
                  echodesk_capabilities,
                  echodesk_control_capability,
                  wifi_time_has_credentials() ? "true" : "false",
                  wifi_time_is_connected() ? "true" : "false",
                  wifi_time_is_synced() ? "true" : "false",
                  (unsigned long)(millis() / 1000),
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned long)board_frame_glitches());
}

#if defined(ECHODESK_OS)
static void print_echodesk_state(uint32_t request_id = 0,
                                 bool include_request_id = false) {
    JsonDocument response;
    response["type"] = "echodesk_state";
    response["version"] = ECHODESK_CONTROL_VERSION;
    if (include_request_id) response["requestId"] = request_id;
    response["revision"] = echodesk_settings_revision;
    response["deviceName"] = "EchoDesk";
    response["hardwareModel"] = "Freenove FNK0114A_2P4";
    response["firmwareVersion"] = APP_VERSION;
    response["activeApp"] = ui_echodesk_active_app_name();
    response["setupState"] = echodesk_setup_state_name(g_config.setup_state);
    response["setupVersion"] = ECHODESK_SETUP_VERSION;
    response["setupTransport"] =
        echodesk_setup_transport_name(g_config.setup_transport);
    if (strncmp(ECHODESK_SETUP_URL, "https://", 8) == 0 &&
        strlen(ECHODESK_SETUP_URL) <= 256) {
        response["setupUrl"] = ECHODESK_SETUP_URL;
    }
    JsonArray apps = response["supportedApps"].to<JsonArray>();
    for (const char *app : {"home", "codex", "clock", "performance",
                            "nowPlaying", "settings"}) {
        apps.add(app);
    }
    JsonObject settings = response["settings"].to<JsonObject>();
    settings["theme"] = g_config.theme == THEME_LIGHT ? "light" : "dark";
    settings["brightness"] = g_config.brightness_pct;
    settings["nowPlayingLayout"] =
        echodesk_layout_name(g_config.now_playing_layout);
    settings["nowPlayingBackground"] =
        echodesk_background_name(g_config.now_playing_background);
    settings["nowPlayingBackgroundCustomized"] =
        g_config.now_playing_background_customized;
    settings["defaultApp"] = echodesk_app_name(g_config.default_app);
    settings["clockFormat"] = echodesk_clock_format_name(g_config.clock_format);
    settings["timezone"] = echodesk_timezone_name(g_config.timezone);
    settings["codexPercentageMode"] =
        echodesk_codex_percentage_name(g_config.codex_percentage_mode);
    settings["codexPreferredDisplay"] =
        echodesk_codex_display_name(g_config.codex_preferred_display);
    settings["performanceDefaultView"] =
        echodesk_performance_view_name(g_config.performance_default_view);
    JsonArray unsupported = response["unsupportedSettings"].to<JsonArray>();
    (void)unsupported;
    serializeJson(response, Serial);
    Serial.println();
}

static void print_echodesk_ack(uint32_t request_id, const char *command) {
    JsonDocument response;
    response["type"] = "echodesk_ack";
    response["version"] = ECHODESK_CONTROL_VERSION;
    response["requestId"] = request_id;
    response["command"] = command;
    response["revision"] = echodesk_settings_revision;
    serializeJson(response, Serial);
    Serial.println();
}

static void print_echodesk_error(uint32_t request_id, const char *code,
                                 const char *message) {
    JsonDocument response;
    response["type"] = "echodesk_error";
    response["version"] = ECHODESK_CONTROL_VERSION;
    response["requestId"] = request_id;
    response["code"] = code;
    response["message"] = message;
    response["revision"] = echodesk_settings_revision;
    serializeJson(response, Serial);
    Serial.println();
}

static bool echodesk_request_header(JsonDocument &doc, uint32_t &request_id) {
    request_id = doc["requestId"].is<uint32_t>()
                     ? doc["requestId"].as<uint32_t>()
                     : 0;
    if (!doc["version"].is<uint8_t>() ||
        doc["version"].as<uint8_t>() != ECHODESK_CONTROL_VERSION ||
        !doc["requestId"].is<uint32_t>() || request_id == 0) {
        print_echodesk_error(request_id, "invalid_request",
                             "version 1 and nonzero numeric requestId required");
        return false;
    }
    return true;
}

static bool echodesk_setup_request(JsonDocument &doc, uint32_t &request_id) {
    if (!echodesk_request_header(doc, request_id)) return false;
    if (doc.size() != 3) {
        print_echodesk_error(request_id, "invalid_request",
                             "setup command accepts only cmd, version and requestId");
        return false;
    }
    return true;
}

static void handle_echodesk_get_state(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_request_header(doc, request_id)) return;
    print_echodesk_state(request_id, true);
}

static void handle_echodesk_open_app(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_request_header(doc, request_id)) return;
    const char *app = doc["app"];
    if (!app || !ui_echodesk_request_open_app(app)) {
        print_echodesk_error(request_id, "unsupported", "unsupported app");
        return;
    }
    print_echodesk_ack(request_id, "echodesk_open_app");
}

static void handle_echodesk_set_settings(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_request_header(doc, request_id)) return;
    if (!doc["baseRevision"].is<uint32_t>() ||
        !doc["settings"].is<JsonObjectConst>() ||
        (!doc["persist"].isNull() && !doc["persist"].is<bool>())) {
        print_echodesk_error(request_id, "invalid_request",
                             "baseRevision, settings and persist are invalid");
        return;
    }
    JsonObjectConst settings = doc["settings"].as<JsonObjectConst>();
    EchoDeskControlPatch patch{};
    for (JsonPairConst pair : settings) {
        const char *key = pair.key().c_str();
        if (strcmp(key, "theme") == 0) {
            if (!pair.value().is<const char*>()) {
                print_echodesk_error(request_id, "invalid_request", "invalid theme");
                return;
            }
            const char *theme = pair.value().as<const char*>();
            if (strcmp(theme, "light") == 0) patch.values.theme = THEME_LIGHT;
            else if (strcmp(theme, "dark") == 0) patch.values.theme = THEME_DARK;
            else {
                print_echodesk_error(request_id, "invalid_request", "invalid theme");
                return;
            }
            patch.has_theme = true;
        } else if (strcmp(key, "brightness") == 0) {
            if (!pair.value().is<uint8_t>()) {
                print_echodesk_error(request_id, "invalid_request", "invalid brightness");
                return;
            }
            patch.has_brightness = true;
            patch.values.brightness = pair.value().as<uint8_t>();
        } else if (strcmp(key, "nowPlayingLayout") == 0) {
            patch.has_now_playing_layout = echodesk_parse_layout(
                pair.value().as<const char*>(), patch.values.now_playing_layout);
            if (!patch.has_now_playing_layout) {
                print_echodesk_error(request_id, "invalid_request", "invalid Now Playing layout");
                return;
            }
        } else if (strcmp(key, "nowPlayingBackground") == 0) {
            patch.has_now_playing_background = echodesk_parse_background(
                pair.value().as<const char*>(), patch.values.now_playing_background);
            if (!patch.has_now_playing_background) {
                print_echodesk_error(request_id, "invalid_request", "invalid Now Playing background");
                return;
            }
        } else if (strcmp(key, "nowPlayingBackgroundCustomized") == 0) {
            if (!pair.value().is<bool>()) {
                print_echodesk_error(request_id, "invalid_request", "invalid background override");
                return;
            }
            patch.has_now_playing_background_customized = true;
            patch.values.now_playing_background_customized = pair.value().as<bool>();
        } else if (strcmp(key, "defaultApp") == 0) {
            patch.has_default_app = echodesk_parse_app(
                pair.value().as<const char*>(), patch.values.default_app);
            if (!patch.has_default_app) {
                print_echodesk_error(request_id, "invalid_request", "invalid startup app");
                return;
            }
        } else if (strcmp(key, "clockFormat") == 0) {
            patch.has_clock_format = echodesk_parse_clock_format(
                pair.value().as<const char*>(), patch.values.clock_format);
            if (!patch.has_clock_format) {
                print_echodesk_error(request_id, "invalid_request", "invalid clock format");
                return;
            }
        } else if (strcmp(key, "timezone") == 0) {
            patch.has_timezone = echodesk_parse_timezone(
                pair.value().as<const char*>(), patch.values.timezone);
            if (!patch.has_timezone) {
                print_echodesk_error(request_id, "invalid_request", "invalid timezone");
                return;
            }
        } else if (strcmp(key, "codexPercentageMode") == 0) {
            patch.has_codex_percentage_mode = echodesk_parse_codex_percentage(
                pair.value().as<const char*>(), patch.values.codex_percentage_mode);
            if (!patch.has_codex_percentage_mode) {
                print_echodesk_error(request_id, "invalid_request", "invalid Codex percentage mode");
                return;
            }
        } else if (strcmp(key, "codexPreferredDisplay") == 0) {
            patch.has_codex_preferred_display = echodesk_parse_codex_display(
                pair.value().as<const char*>(), patch.values.codex_preferred_display);
            if (!patch.has_codex_preferred_display) {
                print_echodesk_error(request_id, "invalid_request", "invalid Codex display");
                return;
            }
        } else if (strcmp(key, "performanceDefaultView") == 0) {
            patch.has_performance_default_view = echodesk_parse_performance_view(
                pair.value().as<const char*>(), patch.values.performance_default_view);
            if (!patch.has_performance_default_view) {
                print_echodesk_error(request_id, "invalid_request", "invalid Performance view");
                return;
            }
        } else {
            print_echodesk_error(request_id, "unsupported", "unsupported setting");
            return;
        }
    }

    EchoDeskControlSettings current{
        g_config.theme, g_config.brightness_pct, g_config.now_playing_layout,
        g_config.now_playing_background,
        g_config.now_playing_background_customized, g_config.default_app,
        g_config.clock_format, g_config.timezone,
        g_config.codex_percentage_mode, g_config.codex_preferred_display,
        g_config.performance_default_view};
    EchoDeskControlSettings next{};
    const bool persist = doc["persist"] | true;
    const EchoDeskControlApplyResult result = echodesk_apply_settings_patch(
        current, patch, doc["baseRevision"].as<uint32_t>(),
        echodesk_settings_revision, persist, next);
    if (result == EchoDeskControlApplyResult::STALE_REVISION) {
        print_echodesk_error(request_id, "stale_revision",
                             "settings changed on device; refresh and retry");
        echodesk_state_dirty = true;
        return;
    }
    if (result == EchoDeskControlApplyResult::PREVIEW_NOT_SUPPORTED) {
        print_echodesk_error(request_id, "invalid_request",
                             "persist false is supported only for brightness");
        return;
    }
    if (result != EchoDeskControlApplyResult::OK) {
        print_echodesk_error(request_id, "invalid_request", "invalid settings patch");
        return;
    }

    const bool theme_changed = next.theme != current.theme;
    const bool now_playing_changed =
        next.now_playing_layout != current.now_playing_layout ||
        next.now_playing_background != current.now_playing_background ||
        next.now_playing_background_customized !=
            current.now_playing_background_customized;
    const bool clock_changed = next.clock_format != current.clock_format ||
                               next.timezone != current.timezone;
    const bool performance_changed =
        next.performance_default_view != current.performance_default_view;
    g_config.theme = next.theme;
    g_config.brightness_pct = next.brightness;
    g_config.now_playing_layout = next.now_playing_layout;
    g_config.now_playing_background = next.now_playing_background;
    g_config.now_playing_background_customized =
        next.now_playing_background_customized;
    g_config.default_app = next.default_app;
    g_config.clock_format = next.clock_format;
    g_config.timezone = next.timezone;
    g_config.codex_percentage_mode = next.codex_percentage_mode;
    g_config.codex_preferred_display = next.codex_preferred_display;
    g_config.performance_default_view = next.performance_default_view;
    backlight_apply_percent(g_config.brightness_pct);
    if (persist) config_save(g_config);
    if (theme_changed) ui_apply_theme(g_config.theme);
    if (clock_changed) serial_apply_configured_timezone();
    if (performance_changed) ui_performance_apply_default_view();
    if (theme_changed || now_playing_changed || clock_changed) {
        serial_request_ui_rebuild();
    }
    serial_echodesk_state_changed();
    print_echodesk_ack(request_id, "echodesk_set_settings");
}

static void handle_echodesk_restart(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_request_header(doc, request_id)) return;
    print_echodesk_ack(request_id, "echodesk_restart");
    Serial.flush();
    ui_echodesk_request_restart();
}

static void handle_echodesk_setup_begin(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_setup_request(doc, request_id)) return;
    if (echodesk_setup_normalize_state(g_config.setup_state) ==
        ECHODESK_SETUP_COMPLETED) {
        // Guided setup also handles verified reinstalls of an already-set-up
        // device. Acknowledge without regressing it into onboarding.
        print_echodesk_ack(request_id, "echodesk_setup_begin");
        return;
    }
    if (!echodesk_setup_can_begin(g_config.setup_state)) {
        print_echodesk_error(request_id, "invalid_state",
                             "setup is already completed; factory reset first");
        return;
    }
    g_config.language = LANG_EN;
    g_config.setup_transport = ECHODESK_SETUP_TRANSPORT_USB;
    g_config.setup_state = ECHODESK_SETUP_IN_PROGRESS;
    g_config.setup_version = ECHODESK_SETUP_VERSION;
    config_save(g_config);
    serial_echodesk_state_changed();
    print_echodesk_ack(request_id, "echodesk_setup_begin");
    ui_echodesk_setup_state_changed();
}

static void handle_echodesk_setup_complete(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_setup_request(doc, request_id)) return;
    if (echodesk_setup_normalize_state(g_config.setup_state) ==
        ECHODESK_SETUP_COMPLETED) {
        print_echodesk_ack(request_id, "echodesk_setup_complete");
        return;
    }
    if (!echodesk_setup_can_complete(g_config.setup_state)) {
        print_echodesk_error(request_id, "invalid_state",
                             "setup is already completed");
        return;
    }
    g_config.setup_state = ECHODESK_SETUP_COMPLETED;
    g_config.setup_version = ECHODESK_SETUP_VERSION;
    config_save(g_config);
    serial_echodesk_state_changed();
    print_echodesk_ack(request_id, "echodesk_setup_complete");
    ui_echodesk_setup_state_changed();
}

static void handle_echodesk_factory_reset(JsonDocument &doc) {
    uint32_t request_id;
    if (!echodesk_setup_request(doc, request_id)) return;
    config_factory_reset_setup(g_config);
    serial_echodesk_state_changed();
    print_echodesk_ack(request_id, "echodesk_factory_reset");
    ui_echodesk_setup_state_changed();
}
#endif

// --- reboot ---
static void handle_reboot() {
    Serial.println("{\"type\":\"ok\",\"cmd\":\"reboot\"}");
    delay(200);
    ESP.restart();
}

static bool valid_plugin_key(const char *name) {
    if (!name || strncmp(name, "plugin:", 7) != 0) return false;
    size_t length = strlen(name + 7);
    if (length == 0 || length > 40) return false;
    for (const char *p = name + 7; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')
              || *p == '.' || *p == '-' || *p == '_')) return false;
    }
    return true;
}

// Unlike provider_from_string(), a window key must not silently fall back to
// Claude when a plugin is missing or its ID is malformed.
static int view_type_from_name(const char *name) {
    if (!name) return -1;
    if (strcasecmp(name, "clock") == 0) return VIEW_CLOCK;
    if (valid_plugin_key(name)) return VIEW_PLUGIN;
    for (uint8_t p = PROVIDER_CLAUDE; p <= PROVIDER_CURSOR; ++p) {
        if (strcasecmp(name, provider_info_for_id(p)->wire_keys[0]) == 0) return p;
    }
    return -1;
}

static bool view_type_valid(uint8_t type) {
    return type == VIEW_CLOCK || type == VIEW_PLUGIN || provider_info_for_id(type)->id == type;
}

static const char *view_name(uint8_t index) {
    uint8_t type = view_types[index];
    if (type == VIEW_CLOCK) return "clock";
    if (type == VIEW_PLUGIN) return view_keys[index];
    return provider_info_for_id(type)->wire_keys[0];
}

static void print_view_state() {
    JsonDocument response;
    response["type"] = "view_state";
    response["mode"] = views_automatic ? "automatic" : "manual";
    response["interval"] = view_interval_seconds;
    response["active"] = active_view;
    JsonArray views = response["views"].to<JsonArray>();
    for (uint8_t i = 0; i < view_count; ++i) views.add(view_name(i));
    serializeJson(response, Serial);
    Serial.println();
}

static void persist_touch_view() {
    Preferences prefs;
    // Kein Flash-Schreiben auf Boards ohne dauerhafte Ablage (siehe board.h).
    if (board_persists_config() && prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar("view_active", active_view);
        prefs.end();
    }
    print_view_state();
}

static void handle_set_views(JsonDocument &doc) {
    JsonArray views = doc["views"];
    if (views.isNull() || views.size() == 0 || views.size() > VIEW_MAX) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_views: invalid count\"}");
        return;
    }
    uint8_t types[VIEW_MAX] = {};
    char keys[VIEW_MAX][PLUGIN_VIEW_KEY_BYTES] = {};
    for (size_t i = 0; i < views.size(); ++i) {
        const char *name = views[i].as<const char*>();
        int type = view_type_from_name(name);
        if (type < 0) {
            Serial.println("{\"type\":\"error\",\"message\":\"set_views: invalid content\"}");
            return;
        }
        types[i] = (uint8_t)type;
        if (type == VIEW_PLUGIN) strlcpy(keys[i], name, sizeof(keys[i]));
    }
    const char *mode = doc["mode"] | "manual";
    if (strcmp(mode, "manual") != 0 && strcmp(mode, "automatic") != 0) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_views: invalid mode\"}");
        return;
    }
    int interval = doc["interval"] | 10;
    int active = doc["active"] | 0;
    if (interval < 2 || interval > 3600 || active < 0 || active >= (int)views.size()) {
        Serial.println("{\"type\":\"error\",\"message\":\"set_views: invalid interval or active\"}");
        return;
    }
    bool layout_changed = view_count != views.size();
    for (size_t i = 0; i < views.size(); ++i) {
        layout_changed |= view_types[i] != types[i] || strcmp(view_keys[i], keys[i]) != 0;
    }
    bool changed = layout_changed || views_automatic != (strcmp(mode, "automatic") == 0)
                   || view_interval_seconds != interval || active_view != active;
    view_count = (uint8_t)views.size();
    memcpy(view_types, types, view_count);
    memcpy(view_keys, keys, sizeof(view_keys));
    views_automatic = strcmp(mode, "automatic") == 0;
    view_interval_seconds = (uint16_t)interval;
    if (layout_changed) {
        plugin_scene_clear_all();
        memset(view_states, 0, sizeof(view_states));
        for (uint8_t i = 0; i < view_count; ++i) {
            view_states[i].provider = view_types[i] >= VIEW_PLUGIN ? PROVIDER_CLAUDE : view_types[i];
            strlcpy(view_states[i].provider_label,
                    view_types[i] == VIEW_CLOCK ? "CLOCK" : view_types[i] == VIEW_PLUGIN ? "PLUGIN" : provider_label_from_id(view_types[i]),
                    sizeof(view_states[i].provider_label));
            if (view_types[i] != VIEW_CLOCK && view_types[i] != VIEW_PLUGIN) {
                view_states[i].usage.notice_only = true;
                strlcpy(view_states[i].usage.error, g_language == LANG_DE ? "Lade Provider ..." : "Loading provider ...",
                        sizeof(view_states[i].usage.error));
            }
        }
    }
    if (changed && board_persists_config()) {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putUChar("view_count", view_count);
        prefs.putBytes("view_types", view_types, view_count);
        prefs.putBytes("view_keys", view_keys, sizeof(view_keys));
        prefs.putBool("view_auto", views_automatic);
        prefs.putUShort("view_secs", view_interval_seconds);
        prefs.putUChar("view_active", (uint8_t)active);
        prefs.end();
    }
    views_host_configured = true;
    serial_select_view((uint8_t)active);
    Serial.printf("{\"type\":\"ok\",\"cmd\":\"set_views\",\"count\":%u}\n", (unsigned)view_count);
}

// Dispatch only — each command's logic lives in a focused handler above.
static bool parse_command(JsonDocument &doc) {
    if (!doc["cmd"].is<const char*>()) return false;

    const char *cmd = doc["cmd"];
    Serial.printf("[Serial] Command received: %s\n", cmd);

    if (strcmp(cmd, "set_orientation") == 0) {
        handle_set_orientation(doc);
    } else if (strcmp(cmd, "set_brightness") == 0) {
        handle_set_brightness(doc);
    } else if (strcmp(cmd, "wifi_scan") == 0) {
        wifi_time_print_scan();
    } else if (strcmp(cmd, "wifi_status") == 0) {
        wifi_time_print_status();
    } else if (strcmp(cmd, "wifi_set") == 0) {
        handle_wifi_set(doc);
    } else if (strcmp(cmd, "wifi_forget") == 0) {
        handle_wifi_forget();
    } else if (strcmp(cmd, "standby") == 0) {
        handle_standby();
    } else if (strcmp(cmd, "set_theme") == 0) {
        handle_set_theme(doc);
    } else if (strcmp(cmd, "set_language") == 0) {
        handle_set_language(doc);
    } else if (strcmp(cmd, "get_info") == 0) {
        handle_get_info();
    } else if (strcmp(cmd, "set_views") == 0) {
        handle_set_views(doc);
    } else if (strcmp(cmd, "get_views") == 0) {
        print_view_state();
#if defined(ECHODESK_OS)
    } else if (strcmp(cmd, "echodesk_get_state") == 0) {
        handle_echodesk_get_state(doc);
    } else if (strcmp(cmd, "echodesk_open_app") == 0) {
        handle_echodesk_open_app(doc);
    } else if (strcmp(cmd, "echodesk_set_settings") == 0) {
        handle_echodesk_set_settings(doc);
    } else if (strcmp(cmd, "echodesk_restart") == 0) {
        handle_echodesk_restart(doc);
    } else if (strcmp(cmd, "echodesk_setup_begin") == 0) {
        handle_echodesk_setup_begin(doc);
    } else if (strcmp(cmd, "echodesk_setup_complete") == 0) {
        handle_echodesk_setup_complete(doc);
    } else if (strcmp(cmd, "echodesk_factory_reset") == 0) {
        handle_echodesk_factory_reset(doc);
#endif
    } else if (strcmp(cmd, "reboot") == 0) {
        handle_reboot();
    } else {
        // --- unknown command ---
        Serial.printf("{\"type\":\"error\",\"message\":\"Unknown command: %s\"}\n", cmd);
    }
    return true;
}

// ============================================================
// parse_json sub-steps (B2 — pure extraction from parse_json)
// ============================================================

// displayTime + tzOffsetMinutes + system clock from ISO "time".
static void parse_time_block(JsonDocument &doc) {
    // Read display time from Mac companion app
    const char *dtime = doc["displayTime"];
    if (dtime) {
        strlcpy(display_time, dtime, sizeof(display_time));
    }

    if (doc["tzOffsetMinutes"].is<int>()) {
        apply_timezone_offset_minutes(doc["tzOffsetMinutes"].as<int>());
        // C3: Host has now defined the timezone — wifi_time must not overwrite
        // TZ with its fixed NTP timezone afterwards (only sync UTC via SNTP).
        host_tz_set = true;
    }

    // Set system clock from ISO "time" field (needed for countdown calculations)
    const char *time_str = doc["time"];
    if (time_str) {
        time_t epoch = iso8601_to_epoch(time_str);
        if (epoch > 0) {
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
        }
    }
}

// Legacy primary/weekly aggregate fields (five_hour_* / seven_day_*).
static void parse_usage_windows(JsonObject usage, JsonObject primary,
                                JsonObject secondary, JsonObject tertiary) {
    // --- Primary (Session / 5h) ---
    if (!primary.isNull()) {
        float usedPct = primary["usedPercent"] | 0.0f;
        state.usage.five_hour_utilization = usedPct / 100.0f;
        const int window = primary["windowMinutes"] | 0;
        state.usage.five_hour_window_minutes = window > 0 ? (uint32_t)window : 0;

        const char *resetsAt = primary["resetsAt"];
        if (resetsAt) {
            strlcpy(state.usage.five_hour_resets_at, resetsAt, sizeof(state.usage.five_hour_resets_at));
            state.usage.five_hour_reset_epoch = iso8601_to_epoch(resetsAt);
        }
    }

    // --- Weekly: find field with windowMinutes >= WEEKLY_WINDOW_MINUTES ---
    // Check secondary first, then tertiary
    JsonObject weekly_source;
    int sec_window = secondary["windowMinutes"] | 0;
    int ter_window = tertiary["windowMinutes"] | 0;

    if (sec_window >= WEEKLY_WINDOW_MINUTES) {
        weekly_source = secondary;
    } else if (ter_window >= WEEKLY_WINDOW_MINUTES) {
        weekly_source = tertiary;
    } else {
        // Fallback: use secondary
        weekly_source = secondary;
    }

    if (!weekly_source.isNull()) {
        float usedPct = weekly_source["usedPercent"] | 0.0f;
        state.usage.seven_day_utilization = usedPct / 100.0f;
        const int window = weekly_source["windowMinutes"] | 0;
        state.usage.seven_day_window_minutes = window > 0 ? (uint32_t)window : 0;

        const char *resetsAt = weekly_source["resetsAt"];
        if (resetsAt) {
            strlcpy(state.usage.seven_day_resets_at, resetsAt, sizeof(state.usage.seven_day_resets_at));
            state.usage.seven_day_reset_epoch = iso8601_to_epoch(resetsAt);
        }
    }
}

// Generic usage rows[] (v2.11.0+) with primary/secondary/tertiary fallback
// and Antigravity 3-row backfill.
static void parse_usage_rows(JsonObject usage, JsonObject primary,
                             JsonObject secondary, JsonObject tertiary) {
    // --- Generic usage rows (v2.11.0+) ---
    // Preferred source: usage.rows[] from companion app.
    // Fallback: derive from primary/secondary/tertiary objects.
    JsonArray rows = usage["rows"];
    if (!rows.isNull()) {
        uint8_t idx = 0;
        for (JsonVariant row_var : rows) {
            if (idx >= USAGE_ROW_MAX) break;
            JsonObject row = row_var.as<JsonObject>();
            if (row.isNull()) continue;

            float usedPct = row["usedPercent"] | 0.0f;
            const char *title = row["title"];
            const char *resetsAt = row["resetsAt"];
            set_usage_row(state.usage, idx, state.provider, title, usedPct, resetsAt,
                          row["windowMinutes"] | 0);
            idx++;
        }
    }

    if (state.usage.row_count == 0) {
        const char *pri_reset = primary["resetsAt"] | "";
        set_usage_row(state.usage, 0, state.provider,
                      default_row_title_for_provider(state.provider, 0),
                      primary["usedPercent"] | 0.0f, pri_reset,
                      primary["windowMinutes"] | 0);

        if (!secondary.isNull()) {
            const char *sec_reset = secondary["resetsAt"] | "";
            set_usage_row(state.usage, 1, state.provider,
                          default_row_title_for_provider(state.provider, 1),
                          secondary["usedPercent"] | 0.0f, sec_reset,
                          secondary["windowMinutes"] | 0);
        }

        if (!tertiary.isNull()) {
            const char *ter_reset = tertiary["resetsAt"] | "";
            set_usage_row(state.usage, 2, state.provider,
                          default_row_title_for_provider(state.provider, 2),
                          tertiary["usedPercent"] | 0.0f, ter_reset,
                          tertiary["windowMinutes"] | 0);
        }
    }

    // Antigravity should always render exactly 3 model rows.
    // If the sender delivered fewer rows (e.g. usageRows missing tertiary),
    // backfill from primary/secondary/tertiary objects.
    if (state.provider == PROVIDER_ANTIGRAVITY && state.usage.row_count < 3) {
        JsonObject windows[3] = { primary, secondary, tertiary };
        for (uint8_t i = state.usage.row_count; i < 3; i++) {
            JsonObject src = windows[i];
            if (!src.isNull()) {
                const char *r = src["resetsAt"] | "";
                set_usage_row(state.usage, i, state.provider,
                              default_row_title_for_provider(state.provider, i),
                              src["usedPercent"] | 0.0f, r,
                              src["windowMinutes"] | 0);
            } else {
                set_usage_row(state.usage, i, state.provider,
                              default_row_title_for_provider(state.provider, i),
                              0.0f, "", 0);
            }
        }
    }
}

// providerCost extra-usage block + loginMethod logging.
static void parse_extra_usage(JsonObject usage) {
    // --- Extra usage (providerCost) ---
    JsonObject cost = usage["providerCost"];
    if (!cost.isNull()) {
        float used  = cost["used"] | 0.0f;
        float limit = cost["limit"] | 0.0f;

        if (limit > 0) {
            state.usage.has_extra_usage    = true;
            state.usage.extra_used_credits = used;
            state.usage.extra_monthly_limit = limit;
            state.usage.extra_utilization  = used / limit;
        } else {
            state.usage.has_extra_usage = false;
        }
    }

    // --- Login method as provider info ---
    const char *loginMethod = usage["loginMethod"];
    if (loginMethod) {
        Serial.printf("[Serial] Login: %s\n", loginMethod);
    }
}

// v2.18.0: Zusatz-Credits und Reset Credits (Codex). Fehlt ein Block,
// gilt er als nicht gemeldet; Werte aus dem vorigen Frame bleiben nicht stehen.
static void clear_credits(UsageData &usage) {
    usage.credits_state        = CREDITS_UNKNOWN;
    usage.credits_has_balance  = false;
    usage.credits_balance      = 0.0f;
    usage.reset_credits_count  = 0;
    usage.reset_credits_expiry = 0;
}

static void parse_credits(JsonObject usage) {
    clear_credits(state.usage);

    JsonObject credits = usage["credits"];
    if (!credits.isNull()) {
        const bool available = credits["available"] | false;
        state.usage.credits_state = available ? CREDITS_AVAILABLE : CREDITS_NONE;
        if (available && credits["balance"].is<float>()) {
            state.usage.credits_has_balance = true;
            state.usage.credits_balance = credits["balance"].as<float>();
        }
    }

    JsonObject reset = usage["resetCredits"];
    if (!reset.isNull()) {
        int count = reset["count"] | 0;
        if (count < 0) count = 0;
        if (count > 99) count = 99;
        state.usage.reset_credits_count = (uint8_t)count;
        state.usage.reset_credits_expiry = iso8601_to_epoch(reset["nextExpiresAt"] | "");
    }
}

// ============================================================
// Helper: parse JSON and update state
// ============================================================
static void parse_json(const char *json_str) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_str);
    const size_t frame_bytes = strlen(json_str);

    if (err) {
        Serial.printf("[Serial] JSON parse error: %s\n", err.c_str());
        // Ein kaputter Frame verdraengt keine gueltigen Daten: Er kann fuer
        // ein anderes Fenster bestimmt gewesen sein, und der naechste Frame
        // kommt ohnehin. Den Fehler zeigt nur ein Fenster ohne Daten.
        if (!state.usage.valid) {
            strlcpy(state.status, "JSON Error", sizeof(state.status));
            strlcpy(state.usage.error, err.c_str(), sizeof(state.usage.error));
            view_states[active_view] = state;  // sonst verwirft der nächste Frame den Fehler
        }
        return;
    }

    int frame_id = doc["frameId"] | -1;
    int schema_version = doc["schemaVersion"] | 0;

    // C2: Reject frames carrying a schemaVersion newer than we understand.
    // Missing (0) or <= supported is processed as before (backwards compatible);
    // a greater value means an incompatible future schema, so abort instead of
    // silently parsing with wrong defaults.
    if (schema_version > SUPPORTED_SCHEMA_VERSION) {
        Serial.printf("[Serial] Unsupported schemaVersion %d (max %d)\n",
                      schema_version, SUPPORTED_SCHEMA_VERSION);
        print_frame_error(frame_id, schema_version, "unsupported schemaVersion");
        return;
    }

#if defined(ECHODESK_OS)
    // Music metadata, bounded artwork/background chunks, and compact real-audio
    // bands use the same single-owner AIM1 transport as usage and performance
    // frames. They are consumed before the legacy parser so existing payload
    // behavior is unchanged.
    const char *music_error = nullptr;
    MusicParseResult music_result =
        music_data_parse_json(doc.as<JsonVariantConst>(), millis(), &music_error);
    if (music_result != MusicParseResult::NOT_MUSIC) {
        if (music_result == MusicParseResult::REJECTED) {
            print_frame_error(frame_id, schema_version,
                              music_error ? music_error : "invalid music frame");
        } else {
            new_data_flag = true;
            if (frame_id >= 0) {
                Serial.printf("{\"type\":\"ack\",\"frameId\":%d,\"schemaVersion\":%d,"
                              "\"message\":\"accepted\",\"bytes\":%u,"
                              "\"musicVersion\":%u,\"heap\":%u}\n",
                              frame_id, schema_version, (unsigned)frame_bytes,
                              (unsigned)MUSIC_PROTOCOL_VERSION,
                              (unsigned)ESP.getFreeHeap());
            }
        }
        return;
    }

    // EchoDesk performance frames share the existing line/framed serial
    // transport, but are deliberately consumed before the legacy usage parser.
    // NOT_PERFORMANCE is the only result that falls through, preserving every
    // existing command, Codex usage, and plugin code path unchanged.
    const char *performance_error = nullptr;
    PerformanceParseResult performance_result =
        performance_data_parse_json(doc.as<JsonVariantConst>(), millis(), &performance_error);
    if (performance_result != PerformanceParseResult::NOT_PERFORMANCE) {
        if (performance_result == PerformanceParseResult::REJECTED) {
            print_frame_error(frame_id, schema_version,
                              performance_error ? performance_error : "invalid performance frame");
        } else if (frame_id >= 0) {
            Serial.printf("{\"type\":\"ack\",\"frameId\":%d,\"schemaVersion\":%d,"
                          "\"message\":\"accepted\",\"bytes\":%u,"
                          "\"performanceVersion\":%u,\"heap\":%u}\n",
                          frame_id, schema_version, (unsigned)frame_bytes,
                          (unsigned)PERFORMANCE_PROTOCOL_VERSION,
                          (unsigned)ESP.getFreeHeap());
        }
        return;
    }
#endif

    // Check for command — if handled, skip usage-data parsing
    if (parse_command(doc)) return;

    parse_time_block(doc);

    // Navigate to data[0].usage
    JsonObject data0 = doc["data"][0];
    if (data0.isNull()) {
        Serial.println("[Serial] No data[0] in JSON");
        print_frame_error(frame_id, schema_version, "Missing data[0]");
        // Wie beim Parse-Fehler: gueltige Daten bleiben stehen.
        if (!state.usage.valid) {
            strlcpy(state.status, "JSON Error", sizeof(state.status));
            strlcpy(state.usage.error, "Missing data[0]", sizeof(state.usage.error));
            view_states[active_view] = state;
        }
        return;
    }

    // Plugin scenes have their own schema and never enter the AI usage parser.
    // Validate the target and the whole scene before changing the cached view.
    if (!data0["pluginId"].isNull()) {
        const char *plugin_id = data0["pluginId"];
        char key[PLUGIN_VIEW_KEY_BYTES];
        if (schema_version != 2 || !plugin_id
            || snprintf(key, sizeof(key), "plugin:%s", plugin_id) >= (int)sizeof(key)
            || !valid_plugin_key(key)) {
            print_frame_error(frame_id, schema_version, "invalid plugin ID");
            return;
        }
        int index = data0["viewIndex"] | -1;
        if (index < 0 || index >= view_count || view_types[index] != VIEW_PLUGIN
            || strcmp(view_keys[index], key) != 0) {
            print_frame_error(frame_id, schema_version, "plugin view mismatch");
            return;
        }
        const char *scene_error = nullptr;
        if (!plugin_scene_store((uint8_t)index, data0["scene"].as<JsonObject>(), &scene_error)) {
            print_frame_error(frame_id, schema_version, scene_error ? scene_error : "invalid scene");
            return;
        }
        Serial.printf("{\"type\":\"ack\",\"frameId\":%d,\"schemaVersion\":2,"
                      "\"message\":\"accepted\",\"bytes\":%u,\"provider\":\"PLUGIN\","
                      "\"rows\":0,\"heap\":%u}\n",
                      frame_id, (unsigned)frame_bytes, (unsigned)ESP.getFreeHeap());
        last_host_frame_ms = millis();
        host_frame_seen = true;
        if (index == active_view) new_data_flag = true;
        return;
    }

    if (schema_version == 2) {
        print_frame_error(frame_id, schema_version, "missing plugin ID");
        return;
    }

    // Jeder Frame aktualisiert genau einen Cache-Eintrag. Die sichtbare
    // Anzeige bleibt bei Daten für andere Fenster unverändert.
    const char *frame_provider = data0["provider"] | "claude";
    int frame_type = view_type_from_name(frame_provider);
    if (frame_type < 0 || frame_type == VIEW_CLOCK) {
        print_frame_error(frame_id, schema_version, "invalid provider");
        return;
    }
    int frame_view = data0["viewIndex"] | 0;
    if (data0["viewIndex"].isNull()) {
        // Alte Mac- und Windows-Companions kennen keine Fenster. Ihr Frame
        // aktiviert zur Laufzeit eine einzelne Provider-Anzeige; die im NVS
        // gespeicherte Fensterkonfiguration bleibt für neue Apps erhalten.
        if (view_count != 1 || view_types[0] != frame_type || active_view != 0) {
            view_count = 1;
            view_types[0] = (uint8_t)frame_type;
            views_automatic = false;
            views_host_configured = false;
            memset(view_states, 0, sizeof(view_states));
            serial_select_view(0);
            Serial.println("[Views] Legacy companion: single provider view");
        }
    } else if (frame_view < 0 || frame_view >= view_count || view_types[frame_view] == VIEW_CLOCK) {
        print_frame_error(frame_id, schema_version, "invalid viewIndex");
        return;
    }
    if (frame_type != view_types[frame_view]) {
        print_frame_error(frame_id, schema_version, "provider does not match view");
        return;
    }
    state = view_states[frame_view];

    // --- Provider label (v2.9.0+ envelope field) ---
    // Companion-App sendet "claude", "codex" oder "antigravity" pro Frame.
    // Fallback bleibt "claude" für alte App-Versionen ohne Feld.
    const char *prov = data0["provider"];
    state.provider = provider_from_string(prov);
    strlcpy(state.provider_label, provider_label_from_id(state.provider), sizeof(state.provider_label));

    // --- Hinweis-Frame (v2.15.0) ---
    // Liefert der gewaehlte Provider keine Daten, sendet die App statt eines
    // Usage-Frames einen Hinweis. Vorher sendete sie in dem Fall GAR NICHTS —
    // das Display zeigte dann unbemerkt die Werte des vorher gewaehlten
    // Providers weiter.
    // v2.15.0-beta.3: `fetching` meldet einen laufenden Abruf auf dem Mac.
    // Das Dashboard zeigt dafuer schon immer ein Refresh-Symbol im Kopf, es
    // wurde im seriellen Pfad nur nie gesetzt.
    state.is_fetching = data0["fetching"].as<bool>();

    const char *notice = data0["notice"];
    if (notice != nullptr && notice[0] != '\0') {
        clear_usage_rows(state.usage);
        clear_credits(state.usage);
        strlcpy(state.usage.error, notice, sizeof(state.usage.error));
        state.usage.valid = false;
        state.usage.notice_only = true;
        state.usage.five_hour_utilization = 0.0f;
        state.usage.seven_day_utilization = 0.0f;
        strlcpy(state.status, notice, sizeof(state.status));
        print_frame_ack(frame_id, schema_version, 0, state.provider, 0);
        Serial.printf("[Serial] Notice frame: %s\n", notice);
        view_states[frame_view] = state;
        state = view_states[active_view];
        last_host_frame_ms = millis();
        host_frame_seen = true;
        if (frame_view == active_view) new_data_flag = true;
        return;
    }

    JsonObject usage = data0["usage"];
    if (usage.isNull()) {
        Serial.println("[Serial] No usage object in data[0]");
        print_frame_error(frame_id, schema_version, "Missing usage");
        strlcpy(state.status, "JSON Error", sizeof(state.status));
        strlcpy(state.usage.error, "Missing usage", sizeof(state.usage.error));
        state.usage.valid = false;
        view_states[frame_view] = state;
        state = view_states[active_view];
        if (frame_view == active_view) new_data_flag = true;
        return;
    }

    clear_usage_rows(state.usage);
    // Each frame is authoritative. A missing primary/weekly object means that
    // window is absent, not that the previous frame's aggregate should remain
    // visible. Clearing these fields also lets EchoDesk hide an ended 5-hour
    // session instead of rendering stale data.
    state.usage.five_hour_utilization = 0.0f;
    state.usage.five_hour_resets_at[0] = '\0';
    state.usage.five_hour_reset_epoch = 0;
    state.usage.five_hour_window_minutes = 0;
    state.usage.seven_day_utilization = 0.0f;
    state.usage.seven_day_resets_at[0] = '\0';
    state.usage.seven_day_reset_epoch = 0;
    state.usage.seven_day_window_minutes = 0;
    // Fehlt das Feld (aeltere Apps), zeigt der Host verbrauchte Prozent.
    const char *percent_mode = usage["percentMode"] | "used";
    state.usage.shows_remaining = strcmp(percent_mode, "remaining") == 0;

    JsonObject primary = usage["primary"];
    JsonObject secondary = usage["secondary"];
    JsonObject tertiary = usage["tertiary"];

    parse_usage_windows(usage, primary, secondary, tertiary);
    parse_usage_rows(usage, primary, secondary, tertiary);
    parse_extra_usage(usage);
    parse_credits(usage);

    // Mark data as valid
    state.usage.valid = true;
    state.usage.last_fetch = millis();
    state.usage.error[0] = '\0';
    state.usage.notice_only = false;
    state.token_valid = true;
    strlcpy(state.status, "OK (USB)", sizeof(state.status));
    Serial.printf("[Serial] Parsed: Session=%.0f%% Weekly=%.0f%% rows=%u provider=%s\n",
                  state.usage.five_hour_utilization * 100.0f,
                  state.usage.seven_day_utilization * 100.0f,
                  (unsigned)state.usage.row_count,
                  state.provider_label);
    print_frame_ack(frame_id, schema_version, frame_bytes, state.provider, state.usage.row_count);
    view_states[frame_view] = state;
    state = view_states[active_view];
    last_host_frame_ms = millis();
    host_frame_seen = true;
    if (frame_view == active_view) new_data_flag = true;
}

// ============================================================
// Init
// ============================================================
void serial_receiver_init() {
    serial_buf_pos = 0;
    serial_discard_until_newline = false;
    serial_receiving_frame = false;
    serial_frame_expected = 0;
    serial_frame_received = 0;
    host_frame_seen = false;
#if defined(ECHODESK_OS)
    performance_data_init();
    music_data_init();
#endif
    last_host_frame_ms = 0;
    memset(&state, 0, sizeof(state));
    usage_data_clear(state.usage);
    state.token_valid = false;

    // Default timezone until the Mac companion sends its selected offset.
    apply_timezone_offset_minutes(timezone_offset_minutes);
    g_language = g_config.language;  // Apply saved language
    state.provider = PROVIDER_CLAUDE;
    strlcpy(state.provider_label, "CLAUDE", sizeof(state.provider_label));
    strlcpy(state.status, L(STR_WAITING), sizeof(state.status));
    memset(view_states, 0, sizeof(view_states));
    view_states[0] = state;
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    uint8_t stored_count = prefs.getUChar("view_count", 1);
    if (stored_count >= 1 && stored_count <= VIEW_MAX &&
        prefs.getBytesLength("view_types") == stored_count) {
        uint8_t stored_types[VIEW_MAX] = {};
        char stored_keys[VIEW_MAX][PLUGIN_VIEW_KEY_BYTES] = {};
        prefs.getBytes("view_types", stored_types, stored_count);
        if (prefs.getBytesLength("view_keys") == sizeof(stored_keys))
            prefs.getBytes("view_keys", stored_keys, sizeof(stored_keys));
        bool valid = true;
        for (uint8_t i = 0; i < stored_count; ++i) {
            valid &= view_type_valid(stored_types[i]);
            if (stored_types[i] == VIEW_PLUGIN) {
                stored_keys[i][PLUGIN_VIEW_KEY_BYTES - 1] = '\0';
                valid &= valid_plugin_key(stored_keys[i]);
            }
        }
        if (valid) {
            view_count = stored_count;
            memcpy(view_types, stored_types, stored_count);
            memcpy(view_keys, stored_keys, sizeof(view_keys));
        }
    }
    views_automatic = prefs.getBool("view_auto", false);
    view_interval_seconds = prefs.getUShort("view_secs", 10);
    if (view_interval_seconds < 2 || view_interval_seconds > 3600) view_interval_seconds = 10;
    uint8_t stored_active = prefs.getUChar("view_active", 0);
    prefs.end();
    for (uint8_t i = 0; i < view_count; ++i) {
        view_states[i].provider = view_types[i] >= VIEW_PLUGIN ? PROVIDER_CLAUDE : view_types[i];
        strlcpy(view_states[i].provider_label,
                view_types[i] == VIEW_CLOCK ? "CLOCK" : view_types[i] == VIEW_PLUGIN ? "PLUGIN" : provider_label_from_id(view_types[i]),
                sizeof(view_states[i].provider_label));
        if (i > 0 && view_types[i] != VIEW_CLOCK && view_types[i] != VIEW_PLUGIN) {
            view_states[i].usage.notice_only = true;
            strlcpy(view_states[i].usage.error, g_language == LANG_DE ? "Lade Provider ..." : "Loading provider ...",
                    sizeof(view_states[i].usage.error));
        }
    }
    serial_select_view(stored_active < view_count ? stored_active : 0);
    strlcpy(display_time, "--:--", sizeof(display_time));

    Serial.println("[Serial] Receiver initialized — waiting for USB data");
}

// ============================================================
// Tick — call from loop()
// ============================================================
void serial_receiver_tick() {
    if (views_host_configured && views_automatic && view_count > 1 &&
        millis() - last_view_change >= (unsigned long)view_interval_seconds * 1000UL) {
        serial_select_view((active_view + 1) % view_count);
    }
    while (Serial.available()) {
        char c = Serial.read();

        if (serial_receiving_frame) {
            // Der Payload ist eine Zeile ohne Umbruch. Kommt der Umbruch vor
            // der angekuendigten Laenge, sind unterwegs Bytes verloren
            // gegangen. Frame verwerfen und im Zeilenmodus weiterlesen, damit
            // der naechste Header wieder erkannt wird. Sonst zaehlten dessen
            // Bytes zu diesem Frame und sein Rest landete als Bruchstueck
            // (etwa `27,"resetsAt":...`) im JSON-Parser.
            if (c == '\n') {
                Serial.printf("{\"type\":\"error\",\"frameId\":%ld,"
                              "\"message\":\"frame truncated (%u of %u bytes)\"}\n",
                              serial_frame_id,
                              (unsigned)serial_frame_received,
                              (unsigned)serial_frame_expected);
                serial_receiving_frame = false;
                serial_frame_expected = 0;
                serial_frame_received = 0;
                serial_buf_pos = 0;
                continue;
            }
            if (serial_frame_received < SERIAL_FRAME_MAX_SIZE) {
                serial_buf[serial_frame_received++] = c;
            }

            if (serial_frame_received >= serial_frame_expected) {
                serial_buf[serial_frame_received] = '\0';
                parse_json(serial_buf);
                serial_receiving_frame = false;
                serial_frame_expected = 0;
                serial_frame_received = 0;
                serial_buf_pos = 0;
            }
            continue;
        }

        if (serial_discard_until_newline) {
            if (c == '\n') {
                serial_discard_until_newline = false;
                serial_buf_pos = 0;
            }
            continue;
        }

        if (c == '\n') {
            if (serial_buf_pos > 0) {
                serial_buf[serial_buf_pos] = '\0';
                if (!begin_framed_payload_from_header(serial_buf)) {
                    parse_json(serial_buf);
                }
            }
            serial_buf_pos = 0;
        } else if (c != '\r') {
            if (serial_buf_pos < SERIAL_FRAME_MAX_SIZE) {
                serial_buf[serial_buf_pos++] = c;
            } else {
                Serial.println("{\"type\":\"error\",\"message\":\"Serial line overflow\"}");
                serial_discard_until_newline = true;
                serial_buf_pos = 0;
            }
        }
    }

    // C1: Frame resync timeout. If a framed payload was announced (header
    // "AIM1 <len> <id>") but the bytes never fully arrive, serial_receiving_frame
    // would otherwise stay true forever and misinterpret following lines as frame
    // bytes (deadlock). Discard the partial frame so plain-line / new-frame
    // reception recovers. The normal success path completes inside the loop above
    // and clears the flag before reaching here.
    if (serial_receiving_frame &&
        (millis() - serial_frame_started_ms) > SERIAL_FRAME_TIMEOUT_MS) {
        Serial.println("{\"type\":\"error\",\"message\":\"frame timeout\"}");
        serial_receiving_frame = false;
        serial_frame_expected = 0;
        serial_frame_received = 0;
        serial_buf_pos = 0;
        // Der Rest des Payloads kommt eventuell noch: bis zum Zeilenende verwerfen.
        serial_discard_until_newline = true;
    }

    // Check for data timeout
    if (state.usage.valid && state.usage.last_fetch > 0) {
        unsigned long elapsed = millis() - state.usage.last_fetch;
        if (elapsed > DATA_TIMEOUT_MS) {
            strlcpy(state.status, "No data (timeout)", sizeof(state.status));
        }
    }
#if defined(ECHODESK_OS)
    if (echodesk_state_dirty) {
        echodesk_state_dirty = false;
        print_echodesk_state();
    }
#endif
}

// ============================================================
// Get state copy
// ============================================================
MonitorState serial_get_state() {
    MonitorState copy;
    memcpy(&copy, &state, sizeof(MonitorState));
    return copy;
}

bool serial_get_provider_state(uint8_t provider, MonitorState &out) {
    for (uint8_t i = 0; i < view_count; ++i) {
        if (view_types[i] == provider) {
            memcpy(&out, &view_states[i], sizeof(MonitorState));
            return true;
        }
    }

    // Legacy companions expose a single active provider and may not have sent
    // set_views. Keep that wire-compatible path available as a fallback.
    if (state.provider == provider) {
        memcpy(&out, &state, sizeof(MonitorState));
        return true;
    }
    return false;
}

// ============================================================
// Recent data check (< 5 minutes)
// ============================================================
bool serial_has_recent_data() {
    if (!state.usage.valid || state.usage.last_fetch == 0) return false;
    return (millis() - state.usage.last_fetch) < DATA_TIMEOUT_MS;
}

bool serial_has_recent_host_frame() {
    return host_frame_seen && (millis() - last_host_frame_ms) < DATA_TIMEOUT_MS;
}

// ============================================================
// New data flag (auto-resets on read)
// ============================================================
bool serial_has_new_data() {
    if (new_data_flag) {
        new_data_flag = false;
        return true;
    }
    return false;
}

// ============================================================
// C4: Deferred UI rebuild (set by parser, consumed by main loop)
// ============================================================
bool serial_ui_rebuild_requested() {
    return ui_rebuild_pending;
}

void serial_ui_rebuild_clear() {
    ui_rebuild_pending = false;
}

// ============================================================
// C3: True once the Mac companion has set a TZ offset, so wifi_time
// keeps the host-selected timezone and only syncs UTC via NTP.
// ============================================================
bool host_timezone_is_set() {
    return host_tz_set;
}
