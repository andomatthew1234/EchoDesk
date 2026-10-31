#ifndef MUSIC_DATA_H
#define MUSIC_DATA_H

#if !defined(MUSIC_DATA_NO_JSON)
#include <ArduinoJson.h>
#endif
#include <stddef.h>
#include <stdint.h>

constexpr uint8_t MUSIC_PROTOCOL_VERSION = 1;
constexpr size_t MUSIC_ART_MAX_BYTES = 12288;
constexpr size_t MUSIC_ART_CHUNK_BYTES = 1536;
constexpr uint16_t MUSIC_BACKGROUND_WIDTH = 320;
constexpr uint16_t MUSIC_BACKGROUND_HEIGHT = 240;
constexpr size_t MUSIC_BACKGROUND_MAX_BYTES = 32768;
constexpr size_t MUSIC_BACKGROUND_CHUNK_BYTES = 1536;
constexpr uint8_t MUSIC_VISUALIZER_BANDS = 16;
constexpr uint32_t MUSIC_VISUALIZER_STALE_AFTER_MS = 750;
constexpr uint32_t MUSIC_STALE_AFTER_MS = 5000;
constexpr uint32_t MUSIC_DISCONNECTED_AFTER_MS = 15000;

enum class MusicParseResult : uint8_t {
    NOT_MUSIC = 0,
    ACCEPTED,
    REJECTED,
};

enum class MusicPlaybackState : uint8_t {
    STOPPED = 0,
    PAUSED,
    PLAYING,
};

enum class MusicConnectionState : uint8_t {
    NEVER_RECEIVED = 0,
    LIVE,
    STALE,
    DISCONNECTED,
};

enum class MusicVisualizerStatus : uint8_t {
    VISUALIZER_NEVER_RECEIVED = 0,
    VISUALIZER_DISABLED,
    VISUALIZER_UNAVAILABLE,
    VISUALIZER_LIVE,
    VISUALIZER_STALE,
};

struct MusicState {
    uint32_t timestamp_epoch;
    uint32_t received_ms;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint64_t generation;
    MusicPlaybackState playback;
    bool can_play_pause;
    bool can_previous;
    bool can_next;
    char title[97];
    char artist[65];
    char album[65];
    char application[65];
    char art_id[17];
};

struct MusicVisualizerState {
    uint32_t received_ms;
    uint32_t sequence;
    bool enabled;
    bool available;
    uint8_t bands[MUSIC_VISUALIZER_BANDS];
};

void music_data_init();
void music_data_mark_disconnected();
bool music_data_ingest(const MusicState &state, uint32_t now_ms);
bool music_data_get(MusicState &out);
MusicConnectionState music_data_connection_state(uint32_t now_ms);
uint32_t music_data_display_position_ms(const MusicState &state, uint32_t now_ms);

// Album art is committed only after a complete, in-order, bounded JFIF JPEG
// arrives. The active buffer is never modified while a replacement is being
// assembled.
bool music_data_begin_art(const char *id, size_t total_bytes, uint16_t chunks,
                          uint32_t expected_crc32);
bool music_data_append_art(const char *id, uint16_t index, size_t offset,
                           const char *base64_data);
bool music_data_art_available();
const uint8_t *music_data_art_data();
size_t music_data_art_size();
const char *music_data_art_id();
uint32_t music_data_art_revision();

// A companion-rendered, display-ready 320x240 background JPEG. Exact-size
// active and staging allocations keep replacement atomic without reserving two
// maximum-size buffers; the active LVGL source is never overwritten in place.
bool music_data_begin_background(const char *id, size_t total_bytes,
                                 uint16_t chunks, uint32_t expected_crc32);
bool music_data_append_background(const char *id, uint16_t index, size_t offset,
                                  const char *base64_data);
bool music_data_background_available();
const uint8_t *music_data_background_data();
size_t music_data_background_size();
const char *music_data_background_id();
uint32_t music_data_background_revision();
// Called by the UI only after it has dropped the old LVGL image-cache entry
// and moved its descriptor to the current revision. Until then a retired
// allocation remains pinned and a new transfer is rejected safely.
void music_data_release_retired_background();

// Compact real-audio analysis received from the companion. No PCM is retained
// or transmitted: only sixteen normalized band magnitudes are stored.
bool music_data_ingest_visualizer(const MusicVisualizerState &state,
                                  uint32_t now_ms);
bool music_data_get_visualizer(MusicVisualizerState &out);
MusicVisualizerStatus music_data_visualizer_status(uint32_t now_ms);
size_t music_base64_decode(const char *input, uint8_t *output, size_t capacity);
uint32_t music_crc32(const uint8_t *data, size_t size);

#if !defined(MUSIC_DATA_NO_JSON)
MusicParseResult music_data_parse_json(JsonVariantConst root, uint32_t now_ms,
                                       const char **error_out = nullptr);
#endif

#endif // MUSIC_DATA_H
