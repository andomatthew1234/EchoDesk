/**
 * Config Store - NVS-based configuration storage
 *
 * Stores orientation and poll_interval in ESP32 NVS (Preferences).
 * No WiFi, no tokens — USB-Serial only.
 */

#include "config_store.h"
#include "board.h"
#include "config.h"
#include "echodesk_setup.h"
#include "pc_dashboard.h"
#include <Preferences.h>

// ============================================================
// Global config instance
// ============================================================
AppConfig g_config;

static Preferences prefs;

static void set_default_pc_dashboard_config(AppConfig &cfg) {
    const PcDashboardConfig defaults = pc_dashboard_default_config();
    cfg.pc_dashboard_metric_count = defaults.count;
    for (uint8_t i = 0; i < PC_DASHBOARD_WIDGET_LIMIT; ++i) {
        cfg.pc_dashboard_metrics[i] = static_cast<uint8_t>(defaults.metrics[i]);
    }
}

static void normalize_pc_dashboard_config(AppConfig &cfg) {
    PcDashboardConfig candidate{};
    candidate.count = cfg.pc_dashboard_metric_count;
    for (uint8_t i = 0; i < PC_DASHBOARD_WIDGET_LIMIT; ++i) {
        candidate.metrics[i] = static_cast<PcDashboardMetric>(cfg.pc_dashboard_metrics[i]);
    }
    if (!pc_dashboard_validate_config(candidate)) set_default_pc_dashboard_config(cfg);
}

static const char *timezone_name(uint8_t timezone) {
    switch (timezone) {
        case ECHODESK_TIMEZONE_SYDNEY: return "Australia/Sydney";
        case ECHODESK_TIMEZONE_UTC: return "UTC";
        default: return "Australia/Melbourne";
    }
}

static uint8_t timezone_from_name(const String &name) {
    if (name == "Australia/Sydney") return ECHODESK_TIMEZONE_SYDNEY;
    if (name == "UTC") return ECHODESK_TIMEZONE_UTC;
    return ECHODESK_TIMEZONE_MELBOURNE;
}

// ============================================================
// Load config from NVS
// ============================================================
void config_load(AppConfig &cfg) {
    if (!board_persists_config()) {
        // Board ohne dauerhafte Ablage (siehe board.h): Voreinstellungen, bis
        // der Host sein Geraeteprofil schickt.
        cfg.poll_interval_sec = DEFAULT_POLL_INTERVAL_SEC;
        cfg.orientation       = ORIENTATION_PORTRAIT;
        cfg.theme             = THEME_DARK;
        cfg.language          = LANG_DE;
        cfg.brightness_pct    = BRIGHTNESS_DEFAULT_PERCENT;
        cfg.now_playing_layout = NOW_PLAYING_LAYOUT_DEFAULT;
        cfg.now_playing_background = NOW_PLAYING_BACKGROUND_DEFAULT;
        cfg.now_playing_background_customized = false;
        cfg.default_app = ECHODESK_APP_DEFAULT;
        cfg.clock_format = CLOCK_FORMAT_DEFAULT;
        cfg.timezone = ECHODESK_TIMEZONE_DEFAULT;
        cfg.codex_percentage_mode = CODEX_PERCENTAGE_DEFAULT;
        cfg.codex_preferred_display = CODEX_DISPLAY_DEFAULT;
        cfg.performance_default_view = PERFORMANCE_VIEW_DEFAULT;
        set_default_pc_dashboard_config(cfg);
        cfg.setup_state = ECHODESK_SETUP_NOT_CONFIGURED;
        cfg.setup_version = ECHODESK_SETUP_VERSION;
        cfg.setup_transport = ECHODESK_SETUP_TRANSPORT_USB;
#if defined(ECHODESK_OS)
        cfg.language = LANG_EN;
#endif
        Serial.println("[Config] Voreinstellungen (dieses Board speichert nicht)");
        return;
    }

    prefs.begin(NVS_NAMESPACE, true);  // read-only

    cfg.poll_interval_sec = prefs.getUInt("poll_sec", DEFAULT_POLL_INTERVAL_SEC);
    cfg.orientation       = prefs.getUChar("orient", ORIENTATION_PORTRAIT);
    cfg.theme             = prefs.getUChar("theme", THEME_DARK);
#if defined(ECHODESK_OS)
    cfg.language          = prefs.getUChar("lang", LANG_EN);
#else
    cfg.language          = prefs.getUChar("lang", LANG_DE);
#endif
    cfg.brightness_pct    = prefs.getUChar("bright", BRIGHTNESS_DEFAULT_PERCENT);
    cfg.now_playing_layout = prefs.getUChar("np_layout", NOW_PLAYING_LAYOUT_DEFAULT);
    cfg.now_playing_background = prefs.getUChar(
        "np_bg", cfg.now_playing_layout == NOW_PLAYING_LAYOUT_ECHODESK
                     ? NOW_PLAYING_BACKGROUND_ECHODESK
                     : NOW_PLAYING_BACKGROUND_ALBUM_COVER);
    cfg.now_playing_background_customized = prefs.getBool("np_bg_custom", false);
    cfg.default_app = prefs.getUChar("default_app", ECHODESK_APP_DEFAULT);
    cfg.clock_format = prefs.getUChar("clock_fmt", CLOCK_FORMAT_DEFAULT);
    cfg.timezone = timezone_from_name(
        prefs.getString("timezone", timezone_name(ECHODESK_TIMEZONE_DEFAULT)));
    cfg.codex_percentage_mode = prefs.getUChar("codex_pct", CODEX_PERCENTAGE_DEFAULT);
    cfg.codex_preferred_display = prefs.getUChar("codex_view", CODEX_DISPLAY_DEFAULT);
    cfg.performance_default_view = prefs.getUChar("perf_view", PERFORMANCE_VIEW_DEFAULT);
    if (prefs.isKey("pcd_count")) {
        cfg.pc_dashboard_metric_count = prefs.getUChar("pcd_count", 0);
        for (uint8_t i = 0; i < PC_DASHBOARD_WIDGET_LIMIT; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "pcd_%u", static_cast<unsigned>(i));
            cfg.pc_dashboard_metrics[i] = prefs.getUChar(key, 0xFF);
        }
        normalize_pc_dashboard_config(cfg);
    } else {
        set_default_pc_dashboard_config(cfg);
    }
    const bool has_setup_state = prefs.isKey("setup_state");
    // Every released EchoDesk OS before guided setup wrote at least one of
    // these keys during its first boot. Treat that as an already configured
    // device so an update never forces an existing customer into onboarding.
    const bool has_legacy_config = prefs.isKey("orient") || prefs.isKey("bright") ||
                                   prefs.isKey("theme") || prefs.isKey("default_app");
    cfg.setup_state = echodesk_setup_initial_state(
        has_setup_state,
        prefs.getUChar("setup_state", ECHODESK_SETUP_NOT_CONFIGURED),
        has_legacy_config);
    cfg.setup_version = prefs.getUChar("setup_ver", ECHODESK_SETUP_VERSION);
    cfg.setup_transport = echodesk_setup_normalize_transport(
        prefs.getUChar("setup_link", ECHODESK_SETUP_TRANSPORT_USB));
    if (cfg.brightness_pct < BRIGHTNESS_MIN_PERCENT) cfg.brightness_pct = BRIGHTNESS_MIN_PERCENT;
    if (cfg.brightness_pct > BRIGHTNESS_MAX_PERCENT) cfg.brightness_pct = BRIGHTNESS_MAX_PERCENT;
    if (cfg.now_playing_layout > NOW_PLAYING_LAYOUT_ECHODESK) {
        cfg.now_playing_layout = NOW_PLAYING_LAYOUT_DEFAULT;
    }
    if (cfg.now_playing_background > NOW_PLAYING_BACKGROUND_AUDIO_BARS) {
        cfg.now_playing_background_customized = false;
    }
    if (!cfg.now_playing_background_customized) {
        cfg.now_playing_background =
            cfg.now_playing_layout == NOW_PLAYING_LAYOUT_ECHODESK
                ? NOW_PLAYING_BACKGROUND_ECHODESK
                : NOW_PLAYING_BACKGROUND_ALBUM_COVER;
    }
    if (cfg.default_app > ECHODESK_APP_BEDSIDE) {
        cfg.default_app = ECHODESK_APP_DEFAULT;
    }
    if (cfg.clock_format > CLOCK_FORMAT_24_HOUR) {
        cfg.clock_format = CLOCK_FORMAT_DEFAULT;
    }
    if (cfg.timezone > ECHODESK_TIMEZONE_UTC) {
        cfg.timezone = ECHODESK_TIMEZONE_DEFAULT;
    }
    if (cfg.codex_percentage_mode > CODEX_PERCENTAGE_REMAINING) {
        cfg.codex_percentage_mode = CODEX_PERCENTAGE_DEFAULT;
    }
    if (cfg.codex_preferred_display > CODEX_DISPLAY_WEEKLY) {
        cfg.codex_preferred_display = CODEX_DISPLAY_DEFAULT;
    }
    if (cfg.performance_default_view > PERFORMANCE_VIEW_MEMORY) {
        cfg.performance_default_view = PERFORMANCE_VIEW_DEFAULT;
    }
    normalize_pc_dashboard_config(cfg);
    cfg.setup_state = echodesk_setup_normalize_state(cfg.setup_state);
    if (cfg.setup_version == 0 || cfg.setup_version > ECHODESK_SETUP_VERSION) {
        cfg.setup_version = ECHODESK_SETUP_VERSION;
    }

    prefs.end();

    if (!has_setup_state && has_legacy_config) {
        Serial.println("[Config] Migrating existing EchoDesk as setup completed");
        config_save(cfg);
    }

    Serial.println("[Config] Loaded from NVS");
    Serial.printf("[Config] Poll interval: %u sec\n", cfg.poll_interval_sec);
    const char *orient_str;
    switch (cfg.orientation) {
        case ORIENTATION_LANDSCAPE_LEFT:  orient_str = "landscape_left";  break;
        case ORIENTATION_LANDSCAPE_RIGHT: orient_str = "landscape_right"; break;
        case ORIENTATION_PORTRAIT:
        default:                          orient_str = "portrait";        break;
    }
    Serial.printf("[Config] Orientation: %s\n", orient_str);
    Serial.printf("[Config] Theme: %s\n",
                  cfg.theme == THEME_LIGHT ? "light" : "dark");
    Serial.printf("[Config] Language: %s\n",
                  cfg.language == LANG_EN ? "en" : "de");
    Serial.printf("[Config] Brightness: %u%%\n", cfg.brightness_pct);
    Serial.printf("[Config] Now Playing layout: %u\n", cfg.now_playing_layout);
    Serial.printf("[Config] Now Playing background: %u (%s)\n",
                  cfg.now_playing_background,
                  cfg.now_playing_background_customized ? "custom" : "layout default");
    Serial.printf("[Config] EchoDesk startup app: %u\n", cfg.default_app);
    Serial.printf("[Config] EchoDesk clock: %s, timezone: %s\n",
                  cfg.clock_format == CLOCK_FORMAT_24_HOUR ? "24h" : "12h",
                  timezone_name(cfg.timezone));
    Serial.printf("[Config] EchoDesk Codex: %s, display: %u\n",
                  cfg.codex_percentage_mode == CODEX_PERCENTAGE_REMAINING
                      ? "remaining" : "used",
                  cfg.codex_preferred_display);
    Serial.printf("[Config] EchoDesk Performance default: %u\n",
                  cfg.performance_default_view);
    Serial.printf("[Config] EchoDesk PC Dashboard widgets: %u\n",
                  cfg.pc_dashboard_metric_count);
    Serial.printf("[Config] EchoDesk setup: %s (v%u)\n",
                  echodesk_setup_state_name(cfg.setup_state),
                  static_cast<unsigned>(cfg.setup_version));
    Serial.printf("[Config] EchoDesk setup transport: %s\n",
                  echodesk_setup_transport_name(cfg.setup_transport));
}

// ============================================================
// Save config to NVS
// ============================================================
void config_save(const AppConfig &cfg) {
    if (!board_persists_config()) {
        // Absichtlich nichts tun: dieses Board speichert nicht (siehe board.h).
        return;
    }

    prefs.begin(NVS_NAMESPACE, false);  // read-write

    prefs.putUInt("poll_sec", cfg.poll_interval_sec);
    prefs.putUChar("orient",  cfg.orientation);
    prefs.putUChar("theme",   cfg.theme);
    prefs.putUChar("lang",    cfg.language);
    prefs.putUChar("bright",  cfg.brightness_pct);
    prefs.putUChar("np_layout", cfg.now_playing_layout);
    prefs.putUChar("np_bg", cfg.now_playing_background);
    prefs.putBool("np_bg_custom", cfg.now_playing_background_customized);
    prefs.putUChar("default_app", cfg.default_app);
    prefs.putUChar("clock_fmt", cfg.clock_format);
    // Store the public IANA identifier, not the implementation-specific POSIX
    // rule used by newlib at runtime.
    prefs.putString("timezone", timezone_name(cfg.timezone));
    prefs.putUChar("codex_pct", cfg.codex_percentage_mode);
    prefs.putUChar("codex_view", cfg.codex_preferred_display);
    prefs.putUChar("perf_view", cfg.performance_default_view);
    prefs.putUChar("pcd_count", cfg.pc_dashboard_metric_count);
    for (uint8_t i = 0; i < PC_DASHBOARD_WIDGET_LIMIT; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "pcd_%u", static_cast<unsigned>(i));
        prefs.putUChar(key, cfg.pc_dashboard_metrics[i]);
    }
    prefs.putUChar("setup_state", cfg.setup_state);
    prefs.putUChar("setup_ver", cfg.setup_version);
    prefs.putUChar("setup_link", cfg.setup_transport);

    prefs.end();
    Serial.println("[Config] Saved to NVS");
}

void config_factory_reset_setup(AppConfig &cfg) {
    cfg.setup_state = ECHODESK_SETUP_NOT_CONFIGURED;
    cfg.setup_version = ECHODESK_SETUP_VERSION;
    cfg.setup_transport = ECHODESK_SETUP_TRANSPORT_USB;
    cfg.language = LANG_EN;
    config_save(cfg);
    Serial.println("[Config] Guided setup reset without erasing unrelated flash data");
}
