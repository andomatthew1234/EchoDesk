/**
 * Config Store - NVS-based configuration storage
 *
 * Stores orientation and poll_interval in ESP32 NVS (Preferences).
 * No WiFi, no tokens — USB-Serial only.
 */

#include "config_store.h"
#include "board.h"
#include "config.h"
#include <Preferences.h>

// ============================================================
// Global config instance
// ============================================================
AppConfig g_config;

static Preferences prefs;

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
        Serial.println("[Config] Voreinstellungen (dieses Board speichert nicht)");
        return;
    }

    prefs.begin(NVS_NAMESPACE, true);  // read-only

    cfg.poll_interval_sec = prefs.getUInt("poll_sec", DEFAULT_POLL_INTERVAL_SEC);
    cfg.orientation       = prefs.getUChar("orient", ORIENTATION_PORTRAIT);
    cfg.theme             = prefs.getUChar("theme", THEME_DARK);
    cfg.language          = prefs.getUChar("lang", LANG_DE);
    cfg.brightness_pct    = prefs.getUChar("bright", BRIGHTNESS_DEFAULT_PERCENT);
    cfg.now_playing_layout = prefs.getUChar("np_layout", NOW_PLAYING_LAYOUT_DEFAULT);
    cfg.now_playing_background = prefs.getUChar(
        "np_bg", cfg.now_playing_layout == NOW_PLAYING_LAYOUT_ECHODESK
                     ? NOW_PLAYING_BACKGROUND_ECHODESK
                     : NOW_PLAYING_BACKGROUND_ALBUM_COVER);
    cfg.now_playing_background_customized = prefs.getBool("np_bg_custom", false);
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

    prefs.end();

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

    prefs.end();
    Serial.println("[Config] Saved to NVS");
}
