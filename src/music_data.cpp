#include "music_data.h"

#include <string.h>
#include <stdlib.h>

namespace {

MusicState *current = nullptr;
bool has_state = false;
bool explicitly_disconnected = false;

uint8_t *art_buffers[2] = {nullptr, nullptr};
uint8_t active_art_buffer = 0;
size_t active_art_size = 0;
char *active_art_id = nullptr;
uint32_t art_revision = 0;

bool art_receiving = false;
uint8_t staging_art_buffer = 1;
size_t staging_total = 0;
size_t staging_received = 0;
uint16_t staging_chunks = 0;
uint16_t staging_next_chunk = 0;
uint32_t staging_crc32 = 0;
char *staging_art_id = nullptr;

struct BackgroundCache {
    uint8_t *slots[2];
    uint8_t active_slot;
    size_t active_size;
    char *active_id;
    uint32_t revision;
    bool receiving;
    uint8_t staging_slot;
    size_t staging_total;
    size_t staging_received;
    uint16_t staging_chunks;
    uint16_t staging_next_chunk;
    uint32_t staging_crc32;
    char *staging_id;
    bool retired_pending;
    uint8_t retired_slot;
};

struct VisualizerCache {
    MusicVisualizerState state;
    bool has_state;
    bool disconnected;
};

BackgroundCache *background_cache = nullptr;
VisualizerCache *visualizer_cache = nullptr;

#define background_slots (background_cache->slots)
#define active_background_slot (background_cache->active_slot)
#define active_background_size (background_cache->active_size)
#define active_background_id (background_cache->active_id)
#define background_revision (background_cache->revision)
#define background_receiving (background_cache->receiving)
#define staging_background_slot (background_cache->staging_slot)
#define staging_background_total (background_cache->staging_total)
#define staging_background_received (background_cache->staging_received)
#define staging_background_chunks (background_cache->staging_chunks)
#define staging_background_next_chunk (background_cache->staging_next_chunk)
#define staging_background_crc32 (background_cache->staging_crc32)
#define staging_background_id (background_cache->staging_id)
#define visualizer (visualizer_cache->state)
#define has_visualizer (visualizer_cache->has_state)
#define visualizer_disconnected (visualizer_cache->disconnected)

bool valid_id(const char *id) {
    if (!id) return false;
    const size_t length = strlen(id);
    if (length == 0 || length > 16) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
    }
    return true;
}

void copy_cstr(char *destination, const char *source, size_t capacity) {
    if (capacity == 0) return;
    const size_t length = strlen(source);
    const size_t count = length < capacity - 1 ? length : capacity - 1;
    memcpy(destination, source, count);
    destination[count] = '\0';
}

bool terminated(const char *value, size_t capacity) {
    return value && memchr(value, '\0', capacity) != nullptr;
}

bool valid_state(const MusicState &value) {
    return value.timestamp_epoch > 0 && value.generation > 0 &&
           value.position_ms <= 24UL * 60UL * 60UL * 1000UL &&
           value.duration_ms <= 24UL * 60UL * 60UL * 1000UL &&
           (value.duration_ms == 0 || value.position_ms <= value.duration_ms + 5000UL) &&
           static_cast<uint8_t>(value.playback) <= static_cast<uint8_t>(MusicPlaybackState::PLAYING) &&
           terminated(value.title, sizeof(value.title)) &&
           terminated(value.artist, sizeof(value.artist)) &&
           terminated(value.album, sizeof(value.album)) &&
           terminated(value.application, sizeof(value.application)) &&
           terminated(value.art_id, sizeof(value.art_id)) &&
           (value.art_id[0] == '\0' || valid_id(value.art_id));
}

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool valid_jfif(const uint8_t *data, size_t size, uint16_t expected_width,
                uint16_t expected_height) {
    static const uint8_t signature[] = {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46
    };
    if (size < sizeof(signature) + 2 ||
        memcmp(data, signature, sizeof(signature)) != 0 ||
        data[size - 2] != 0xFF || data[size - 1] != 0xD9) return false;

    // Validate the encoded dimensions rather than trusting the JSON fields.
    // Only baseline SOF0 is accepted; the companion intentionally emits a
    // baseline JFIF and LVGL therefore never receives a progressive or
    // unexpectedly large image through this path.
    size_t offset = 2;
    while (offset + 1 < size) {
        if (data[offset++] != 0xFF) continue;
        while (offset < size && data[offset] == 0xFF) ++offset;
        if (offset >= size) return false;
        const uint8_t marker = data[offset++];
        if (marker == 0xD8) continue;
        if (marker == 0xD9) break;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (offset + 2 > size) return false;
        const size_t segment_size =
            (static_cast<size_t>(data[offset]) << 8) | data[offset + 1];
        if (segment_size < 2 || segment_size > size - offset) return false;
        if (marker == 0xC0) {
            if (segment_size < 8 || data[offset + 2] != 8) return false;
            const uint16_t height = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset + 3]) << 8) |
                data[offset + 4]);
            const uint16_t width = static_cast<uint16_t>(
                (static_cast<uint16_t>(data[offset + 5]) << 8) |
                data[offset + 6]);
            return width == expected_width && height == expected_height;
        }
        if (marker == 0xDA) return false;
        offset += segment_size;
    }
    return false;
}

#if !defined(MUSIC_DATA_NO_JSON)
bool copy_string(JsonVariantConst source, char *destination, size_t capacity) {
    if (source.isNull()) {
        destination[0] = '\0';
        return true;
    }
    if (!source.is<const char*>()) return false;
    const char *value = source.as<const char*>();
    if (!value || strlen(value) >= capacity) return false;
    strlcpy(destination, value, capacity);
    return true;
}
#endif

} // namespace

void music_data_init() {
    if (!current) current = static_cast<MusicState *>(malloc(sizeof(MusicState)));
    if (current) memset(current, 0, sizeof(*current));
    has_state = false;
    explicitly_disconnected = false;
    active_art_size = 0;
    if (!active_art_id) active_art_id = static_cast<char *>(malloc(17));
    if (!staging_art_id) staging_art_id = static_cast<char *>(malloc(17));
    if (active_art_id) active_art_id[0] = '\0';
    art_revision = 0;
    art_receiving = false;
    staging_received = 0;
    if (!art_buffers[0]) art_buffers[0] = static_cast<uint8_t *>(malloc(MUSIC_ART_MAX_BYTES));
    if (!art_buffers[1]) art_buffers[1] = static_cast<uint8_t *>(malloc(MUSIC_ART_MAX_BYTES));
    if (!art_buffers[0] || !art_buffers[1] || !active_art_id || !staging_art_id) {
        free(art_buffers[0]);
        free(art_buffers[1]);
        art_buffers[0] = nullptr;
        art_buffers[1] = nullptr;
        free(active_art_id);
        free(staging_art_id);
        active_art_id = nullptr;
        staging_art_id = nullptr;
    }

    if (!background_cache) {
        background_cache = static_cast<BackgroundCache *>(calloc(1, sizeof(BackgroundCache)));
    }
    if (background_cache) {
        free(background_slots[0]);
        free(background_slots[1]);
        background_slots[0] = nullptr;
        background_slots[1] = nullptr;
        active_background_slot = 0;
        active_background_size = 0;
        staging_background_slot = 1;
        if (!active_background_id) active_background_id = static_cast<char *>(malloc(17));
        if (!staging_background_id) staging_background_id = static_cast<char *>(malloc(17));
        if (active_background_id) active_background_id[0] = '\0';
        background_revision = 0;
        background_receiving = false;
        background_cache->retired_pending = false;
        staging_background_received = 0;
        if (!active_background_id || !staging_background_id) {
            free(active_background_id);
            free(staging_background_id);
            active_background_id = nullptr;
            staging_background_id = nullptr;
        }
    }

    if (!visualizer_cache) {
        visualizer_cache = static_cast<VisualizerCache *>(calloc(1, sizeof(VisualizerCache)));
    }
    if (visualizer_cache) memset(visualizer_cache, 0, sizeof(*visualizer_cache));
}

void music_data_mark_disconnected() {
    explicitly_disconnected = true;
    art_receiving = false;
    if (background_cache && background_receiving) {
        free(background_slots[staging_background_slot]);
        background_slots[staging_background_slot] = nullptr;
    }
    if (background_cache) background_receiving = false;
    if (visualizer_cache) visualizer_disconnected = true;
}

bool music_data_ingest(const MusicState &input, uint32_t now_ms) {
    MusicState next = input;
    next.received_ms = now_ms;
    if (!valid_state(next)) return false;
    if (next.duration_ms > 0 && next.position_ms > next.duration_ms) {
        next.position_ms = next.duration_ms;
    }
    if (!current) return false;
    *current = next;
    has_state = true;
    explicitly_disconnected = false;
    return true;
}

bool music_data_get(MusicState &out) {
    if (!has_state) return false;
    if (!current) return false;
    out = *current;
    return true;
}

MusicConnectionState music_data_connection_state(uint32_t now_ms) {
    if (explicitly_disconnected) return MusicConnectionState::DISCONNECTED;
    if (!has_state) return MusicConnectionState::NEVER_RECEIVED;
    if (!current) return MusicConnectionState::NEVER_RECEIVED;
    const uint32_t age = now_ms - current->received_ms;
    if (age <= MUSIC_STALE_AFTER_MS) return MusicConnectionState::LIVE;
    if (age <= MUSIC_DISCONNECTED_AFTER_MS) return MusicConnectionState::STALE;
    return MusicConnectionState::DISCONNECTED;
}

uint32_t music_data_display_position_ms(const MusicState &state, uint32_t now_ms) {
    uint64_t position = state.position_ms;
    if (state.playback == MusicPlaybackState::PLAYING) {
        position += static_cast<uint32_t>(now_ms - state.received_ms);
    }
    if (state.duration_ms > 0 && position > state.duration_ms) position = state.duration_ms;
    return static_cast<uint32_t>(position);
}

bool music_data_begin_art(const char *id, size_t total_bytes, uint16_t chunks,
                          uint32_t expected_crc32) {
    if (!art_buffers[0] || !art_buffers[1] || !active_art_id || !staging_art_id ||
        !has_state || !current || strcmp(current->art_id, id ? id : "") != 0 ||
        !valid_id(id) ||
        total_bytes == 0 || total_bytes > MUSIC_ART_MAX_BYTES ||
        chunks == 0 || chunks > (MUSIC_ART_MAX_BYTES + MUSIC_ART_CHUNK_BYTES - 1) /
                               MUSIC_ART_CHUNK_BYTES) return false;
    staging_art_buffer = active_art_buffer ^ 1u;
    staging_total = total_bytes;
    staging_received = 0;
    staging_chunks = chunks;
    staging_next_chunk = 0;
    staging_crc32 = expected_crc32;
    copy_cstr(staging_art_id, id, 17);
    art_receiving = true;
    return true;
}

bool music_data_append_art(const char *id, uint16_t index, size_t offset,
                           const char *base64_data) {
    if (!art_receiving || !id || strcmp(id, staging_art_id) != 0 ||
        index != staging_next_chunk || index >= staging_chunks ||
        offset != staging_received || !base64_data) return false;
    const size_t remaining = staging_total - staging_received;
    const size_t capacity = remaining < MUSIC_ART_CHUNK_BYTES ? remaining : MUSIC_ART_CHUNK_BYTES;
    const size_t decoded = music_base64_decode(
        base64_data, art_buffers[staging_art_buffer] + staging_received, capacity);
    if (decoded == 0 || decoded > remaining ||
        (index + 1 < staging_chunks && decoded != MUSIC_ART_CHUNK_BYTES)) {
        art_receiving = false;
        return false;
    }
    staging_received += decoded;
    ++staging_next_chunk;
    if (staging_next_chunk == staging_chunks) {
        art_receiving = false;
        if (staging_received != staging_total ||
            music_crc32(art_buffers[staging_art_buffer], staging_total) != staging_crc32 ||
            !valid_jfif(art_buffers[staging_art_buffer], staging_total, 96, 96)) return false;
        active_art_buffer = staging_art_buffer;
        active_art_size = staging_total;
        copy_cstr(active_art_id, staging_art_id, 17);
        ++art_revision;
    }
    return true;
}

bool music_data_art_available() { return active_art_size > 0; }
const uint8_t *music_data_art_data() { return art_buffers[active_art_buffer]; }
size_t music_data_art_size() { return active_art_size; }
const char *music_data_art_id() { return active_art_id ? active_art_id : ""; }
uint32_t music_data_art_revision() { return art_revision; }

bool music_data_begin_background(const char *id, size_t total_bytes,
                                 uint16_t chunks, uint32_t expected_crc32) {
    if (!background_cache) return false;
    // An index-zero header always starts a new transaction. Discard only the
    // incomplete staging allocation; the active image remains untouched.
    background_receiving = false;
    staging_background_slot = active_background_slot ^ 1u;
    if (background_cache->retired_pending &&
        staging_background_slot == background_cache->retired_slot) {
        return false;
    }
    free(background_slots[staging_background_slot]);
    background_slots[staging_background_slot] = nullptr;
    if (!active_background_id || !staging_background_id ||
        !has_state || !current || strcmp(current->art_id, id ? id : "") != 0 ||
        !valid_id(id) || total_bytes == 0 || total_bytes > MUSIC_BACKGROUND_MAX_BYTES ||
        chunks == 0 ||
        chunks > (MUSIC_BACKGROUND_MAX_BYTES + MUSIC_BACKGROUND_CHUNK_BYTES - 1) /
                     MUSIC_BACKGROUND_CHUNK_BYTES) {
        return false;
    }
    background_slots[staging_background_slot] =
        static_cast<uint8_t *>(malloc(total_bytes));
    if (!background_slots[staging_background_slot]) {
        background_receiving = false;
        return false;
    }
    staging_background_total = total_bytes;
    staging_background_received = 0;
    staging_background_chunks = chunks;
    staging_background_next_chunk = 0;
    staging_background_crc32 = expected_crc32;
    copy_cstr(staging_background_id, id, 17);
    background_receiving = true;
    return true;
}

bool music_data_append_background(const char *id, uint16_t index, size_t offset,
                                  const char *base64_data) {
    if (!background_cache) return false;
    if (!background_receiving || !id || strcmp(id, staging_background_id) != 0 ||
        index != staging_background_next_chunk || index >= staging_background_chunks ||
        offset != staging_background_received || !base64_data) {
        background_receiving = false;
        free(background_slots[staging_background_slot]);
        background_slots[staging_background_slot] = nullptr;
        return false;
    }
    const size_t remaining = staging_background_total - staging_background_received;
    const size_t capacity = remaining < MUSIC_BACKGROUND_CHUNK_BYTES
                                ? remaining : MUSIC_BACKGROUND_CHUNK_BYTES;
    const size_t decoded = music_base64_decode(
        base64_data,
        background_slots[staging_background_slot] + staging_background_received,
        capacity);
    if (decoded == 0 || decoded > remaining ||
        (index + 1 < staging_background_chunks &&
         decoded != MUSIC_BACKGROUND_CHUNK_BYTES)) {
        background_receiving = false;
        free(background_slots[staging_background_slot]);
        background_slots[staging_background_slot] = nullptr;
        return false;
    }
    staging_background_received += decoded;
    ++staging_background_next_chunk;
    if (staging_background_next_chunk == staging_background_chunks) {
        background_receiving = false;
        if (staging_background_received != staging_background_total ||
            music_crc32(background_slots[staging_background_slot],
                        staging_background_total) !=
                staging_background_crc32 ||
            !valid_jfif(background_slots[staging_background_slot],
                        staging_background_total, MUSIC_BACKGROUND_WIDTH,
                        MUSIC_BACKGROUND_HEIGHT)) {
            free(background_slots[staging_background_slot]);
            background_slots[staging_background_slot] = nullptr;
            return false;
        }
        // Do not free the previous active slot here: LVGL may still hold an
        // image-cache descriptor to it until the UI observes this revision.
        // It becomes the inactive slot and is reclaimed by the next begin.
        const uint8_t previous_active_slot = active_background_slot;
        const bool previous_active_allocated =
            active_background_size > 0 &&
            background_slots[previous_active_slot] != nullptr;
        active_background_slot = staging_background_slot;
        active_background_size = staging_background_total;
        copy_cstr(active_background_id, staging_background_id, 17);
        background_cache->retired_pending = previous_active_allocated;
        background_cache->retired_slot = previous_active_slot;
        ++background_revision;
    }
    return true;
}

bool music_data_background_available() {
    return background_cache && active_background_size > 0;
}
const uint8_t *music_data_background_data() {
    return background_cache ? background_slots[active_background_slot] : nullptr;
}
size_t music_data_background_size() {
    return background_cache ? active_background_size : 0;
}
const char *music_data_background_id() {
    return background_cache && active_background_id ? active_background_id : "";
}
uint32_t music_data_background_revision() {
    return background_cache ? background_revision : 0;
}

void music_data_release_retired_background() {
    if (!background_cache || !background_cache->retired_pending) return;
    const uint8_t slot = background_cache->retired_slot;
    if (slot != active_background_slot) {
        free(background_slots[slot]);
        background_slots[slot] = nullptr;
    }
    background_cache->retired_pending = false;
}

bool music_data_ingest_visualizer(const MusicVisualizerState &input,
                                  uint32_t now_ms) {
    if (!visualizer_cache) return false;
    if (input.available && !input.enabled) return false;
    visualizer = input;
    visualizer.received_ms = now_ms;
    if (!visualizer.available) {
        memset(visualizer.bands, 0, sizeof(visualizer.bands));
    }
    has_visualizer = true;
    visualizer_disconnected = false;
    return true;
}

bool music_data_get_visualizer(MusicVisualizerState &out) {
    if (!visualizer_cache || !has_visualizer) return false;
    out = visualizer;
    return true;
}

MusicVisualizerStatus music_data_visualizer_status(uint32_t now_ms) {
    if (!visualizer_cache || !has_visualizer) {
        return MusicVisualizerStatus::VISUALIZER_NEVER_RECEIVED;
    }
    if (!visualizer.enabled) return MusicVisualizerStatus::VISUALIZER_DISABLED;
    if (!visualizer.available) return MusicVisualizerStatus::VISUALIZER_UNAVAILABLE;
    if (visualizer_disconnected ||
        now_ms - visualizer.received_ms > MUSIC_VISUALIZER_STALE_AFTER_MS) {
        return MusicVisualizerStatus::VISUALIZER_STALE;
    }
    return MusicVisualizerStatus::VISUALIZER_LIVE;
}

size_t music_base64_decode(const char *input, uint8_t *output, size_t capacity) {
    if (!input || !output) return 0;
    const size_t length = strlen(input);
    if (length == 0 || length % 4 != 0) return 0;
    size_t out = 0;
    for (size_t i = 0; i < length; i += 4) {
        const int a = base64_value(input[i]);
        const int b = base64_value(input[i + 1]);
        const int c = input[i + 2] == '=' ? -2 : base64_value(input[i + 2]);
        const int d = input[i + 3] == '=' ? -2 : base64_value(input[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1 ||
            (c == -2 && d != -2) || (i + 4 != length && (c == -2 || d == -2))) return 0;
        const size_t bytes = c == -2 ? 1 : d == -2 ? 2 : 3;
        if (out + bytes > capacity) return 0;
        const uint32_t bits = (static_cast<uint32_t>(a) << 18) |
                              (static_cast<uint32_t>(b) << 12) |
                              (static_cast<uint32_t>(c < 0 ? 0 : c) << 6) |
                              static_cast<uint32_t>(d < 0 ? 0 : d);
        output[out++] = static_cast<uint8_t>(bits >> 16);
        if (bytes > 1) output[out++] = static_cast<uint8_t>(bits >> 8);
        if (bytes > 2) output[out++] = static_cast<uint8_t>(bits);
    }
    return out;
}

uint32_t music_crc32(const uint8_t *data, size_t size) {
    if (!data && size != 0) return 0;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

#if !defined(MUSIC_DATA_NO_JSON)
MusicParseResult music_data_parse_json(JsonVariantConst root, uint32_t now_ms,
                                       const char **error_out) {
    if (error_out) *error_out = nullptr;
    const char *type = root["type"] | "";
    if (strcmp(type, "music") != 0 && strcmp(type, "music_art") != 0 &&
        strcmp(type, "music_background") != 0 &&
        strcmp(type, "music_visualizer") != 0) {
        return MusicParseResult::NOT_MUSIC;
    }
    auto reject = [&](const char *message) {
        if (error_out) *error_out = message;
        return MusicParseResult::REJECTED;
    };
    if (!root["version"].is<uint8_t>() ||
        root["version"].as<uint8_t>() != MUSIC_PROTOCOL_VERSION) {
        return reject("unsupported music version");
    }
    if (strcmp(type, "music_art") == 0) {
        const char *id = root["id"] | "";
        if (!root["index"].is<uint16_t>() || !root["chunks"].is<uint16_t>() ||
            !root["offset"].is<size_t>() || !root["totalBytes"].is<size_t>() ||
            !root["crc32"].is<uint32_t>() || !root["width"].is<uint16_t>() ||
            !root["height"].is<uint16_t>() || !root["data"].is<const char*>() ||
            root["width"].as<uint16_t>() != 96 || root["height"].as<uint16_t>() != 96) {
            return reject("invalid music art fields");
        }
        const uint16_t index = root["index"].as<uint16_t>();
        if (index == 0 && !music_data_begin_art(
                id, root["totalBytes"].as<size_t>(), root["chunks"].as<uint16_t>(),
                root["crc32"].as<uint32_t>())) {
            return reject("invalid music art header");
        }
        if (!music_data_append_art(id, index, root["offset"].as<size_t>(),
                                   root["data"].as<const char*>())) {
            return reject("invalid music art chunk");
        }
        return MusicParseResult::ACCEPTED;
    }
    if (strcmp(type, "music_background") == 0) {
        const char *id = root["id"] | "";
        if (!root["index"].is<uint16_t>() || !root["chunks"].is<uint16_t>() ||
            !root["offset"].is<size_t>() || !root["totalBytes"].is<size_t>() ||
            !root["crc32"].is<uint32_t>() || !root["width"].is<uint16_t>() ||
            !root["height"].is<uint16_t>() || !root["data"].is<const char*>() ||
            root["width"].as<uint16_t>() != MUSIC_BACKGROUND_WIDTH ||
            root["height"].as<uint16_t>() != MUSIC_BACKGROUND_HEIGHT) {
            return reject("invalid music background fields");
        }
        const uint16_t index = root["index"].as<uint16_t>();
        if (index == 0 && !music_data_begin_background(
                id, root["totalBytes"].as<size_t>(), root["chunks"].as<uint16_t>(),
                root["crc32"].as<uint32_t>())) {
            return reject("invalid music background header");
        }
        if (!music_data_append_background(id, index, root["offset"].as<size_t>(),
                                          root["data"].as<const char*>())) {
            return reject("invalid music background chunk");
        }
        return MusicParseResult::ACCEPTED;
    }
    if (strcmp(type, "music_visualizer") == 0) {
        if (!root["available"].is<bool>() || !root["sequence"].is<uint32_t>() ||
            (!root["enabled"].isNull() && !root["enabled"].is<bool>())) {
            return reject("invalid music visualizer fields");
        }
        MusicVisualizerState next{};
        next.sequence = root["sequence"].as<uint32_t>();
        next.enabled = root["enabled"].isNull() ? true : root["enabled"].as<bool>();
        next.available = root["available"].as<bool>();
        if (next.available) {
            JsonArrayConst bands = root["bands"].as<JsonArrayConst>();
            if (bands.isNull() || bands.size() != MUSIC_VISUALIZER_BANDS) {
                return reject("invalid music visualizer bands");
            }
            uint8_t index = 0;
            for (JsonVariantConst band : bands) {
                if (!band.is<uint8_t>()) return reject("invalid music visualizer band");
                next.bands[index++] = band.as<uint8_t>();
            }
        }
        if (!music_data_ingest_visualizer(next, now_ms)) {
            return reject("invalid music visualizer values");
        }
        return MusicParseResult::ACCEPTED;
    }
    if (root["connected"].is<bool>() && !root["connected"].as<bool>()) {
        music_data_mark_disconnected();
        return MusicParseResult::ACCEPTED;
    }
    MusicState next{};
    if (!root["timestamp"].is<uint32_t>() || !root["generation"].is<uint64_t>() ||
        !root["positionMs"].is<uint32_t>() ||
        !root["durationMs"].is<uint32_t>() || !root["state"].is<const char*>() ||
        !root["canPlayPause"].is<bool>() || !root["canPrevious"].is<bool>() ||
        !root["canNext"].is<bool>()) return reject("invalid music metadata fields");
    next.timestamp_epoch = root["timestamp"].as<uint32_t>();
    next.generation = root["generation"].as<uint64_t>();
    next.position_ms = root["positionMs"].as<uint32_t>();
    next.duration_ms = root["durationMs"].as<uint32_t>();
    const char *playback = root["state"].as<const char*>();
    if (strcmp(playback, "playing") == 0) next.playback = MusicPlaybackState::PLAYING;
    else if (strcmp(playback, "paused") == 0) next.playback = MusicPlaybackState::PAUSED;
    else if (strcmp(playback, "stopped") == 0) next.playback = MusicPlaybackState::STOPPED;
    else return reject("invalid music playback state");
    next.can_play_pause = root["canPlayPause"].as<bool>();
    next.can_previous = root["canPrevious"].as<bool>();
    next.can_next = root["canNext"].as<bool>();
    if (!copy_string(root["title"], next.title, sizeof(next.title)) ||
        !copy_string(root["artist"], next.artist, sizeof(next.artist)) ||
        !copy_string(root["album"], next.album, sizeof(next.album)) ||
        !copy_string(root["application"], next.application, sizeof(next.application)) ||
        !copy_string(root["artId"], next.art_id, sizeof(next.art_id))) {
        return reject("music metadata string too long");
    }
    if (!music_data_ingest(next, now_ms)) return reject("invalid music metadata values");
    return MusicParseResult::ACCEPTED;
}
#endif
