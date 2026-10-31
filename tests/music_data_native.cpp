#define MUSIC_DATA_NO_JSON 1
#include "../src/music_data.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

static std::vector<uint8_t> jfif(uint16_t width, uint16_t height) {
    return {
        0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J',  'F',  'I',  'F',
        0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
        0xFF, 0xC0, 0x00, 0x11, 0x08,
        static_cast<uint8_t>(height >> 8), static_cast<uint8_t>(height),
        static_cast<uint8_t>(width >> 8), static_cast<uint8_t>(width),
        0x03, 0x01, 0x11, 0x00, 0x02, 0x11, 0x00, 0x03, 0x11, 0x00,
        0xFF, 0xD9,
    };
}

static std::string base64(const std::vector<uint8_t> &bytes) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t offset = 0; offset < bytes.size(); offset += 3) {
        const size_t remaining = bytes.size() - offset;
        const uint32_t value =
            (static_cast<uint32_t>(bytes[offset]) << 16) |
            (remaining > 1 ? static_cast<uint32_t>(bytes[offset + 1]) << 8 : 0) |
            (remaining > 2 ? bytes[offset + 2] : 0);
        out.push_back(alphabet[(value >> 18) & 63]);
        out.push_back(alphabet[(value >> 12) & 63]);
        out.push_back(remaining > 1 ? alphabet[(value >> 6) & 63] : '=');
        out.push_back(remaining > 2 ? alphabet[value & 63] : '=');
    }
    return out;
}

static MusicState state(uint32_t position, MusicPlaybackState playback) {
    MusicState value{};
    value.timestamp_epoch = 1;
    value.generation = 7;
    value.position_ms = position;
    value.duration_ms = 20000;
    value.playback = playback;
    value.can_play_pause = true;
    strcpy(value.title, "Track");
    strcpy(value.artist, "Artist");
    strcpy(value.album, "Album");
    strcpy(value.application, "Feishin");
    strcpy(value.art_id, "0123456789abcdef");
    return value;
}

int main() {
    music_data_init();
    MusicState value = state(5000, MusicPlaybackState::PLAYING);
    assert(music_data_ingest(value, 1000));
    MusicState latest{};
    assert(music_data_get(latest));
    assert(music_data_display_position_ms(latest, 2500) == 6500);
    latest.playback = MusicPlaybackState::PAUSED;
    assert(music_data_display_position_ms(latest, 9000) == 5000);
    latest.playback = MusicPlaybackState::PLAYING;
    assert(music_data_display_position_ms(latest, 30000) == 20000);
    assert(music_data_connection_state(6000) == MusicConnectionState::LIVE);
    assert(music_data_connection_state(6001) == MusicConnectionState::STALE);
    assert(music_data_connection_state(16001) == MusicConnectionState::DISCONNECTED);

    MusicState invalid = state(0, MusicPlaybackState::STOPPED);
    invalid.generation = 0;
    assert(!music_data_ingest(invalid, 1));
    invalid = state(0, MusicPlaybackState::STOPPED);
    memset(invalid.title, 'x', sizeof(invalid.title));
    assert(!music_data_ingest(invalid, 1));

    uint8_t decoded[16]{};
    assert(music_base64_decode("/9j/4AAQSkZJRv/Z", decoded, sizeof(decoded)) == 12);
    assert(music_base64_decode("not base64", decoded, sizeof(decoded)) == 0);
    const auto art_bytes = jfif(96, 96);
    const std::string art_base64 = base64(art_bytes);
    const uint32_t crc = music_crc32(art_bytes.data(), art_bytes.size());
    assert(music_data_begin_art("0123456789abcdef", art_bytes.size(), 1, crc));
    assert(music_data_append_art("0123456789abcdef", 0, 0, art_base64.c_str()));
    assert(music_data_art_available());
    assert(music_data_art_size() == art_bytes.size());
    assert(strcmp(music_data_art_id(), "0123456789abcdef") == 0);
    const uint32_t revision = music_data_art_revision();

    value = state(5000, MusicPlaybackState::PLAYING);
    strcpy(value.art_id, "fedcba9876543210");
    assert(music_data_ingest(value, 2000));
    assert(music_data_begin_art("fedcba9876543210", art_bytes.size(), 1, crc ^ 1));
    assert(!music_data_append_art("fedcba9876543210", 0, 0, art_base64.c_str()));
    assert(music_data_art_revision() == revision);
    assert(strcmp(music_data_art_id(), "0123456789abcdef") == 0);
    assert(!music_data_begin_art("bad id!", art_bytes.size(), 1, crc));
    assert(!music_data_begin_art("valid", MUSIC_ART_MAX_BYTES + 1, 1, crc));

    const auto background_bytes = jfif(MUSIC_BACKGROUND_WIDTH,
                                       MUSIC_BACKGROUND_HEIGHT);
    const std::string background_base64 = base64(background_bytes);
    const uint32_t background_crc =
        music_crc32(background_bytes.data(), background_bytes.size());
    assert(music_data_begin_background("fedcba9876543210",
                                       background_bytes.size(), 1,
                                       background_crc));
    assert(music_data_append_background("fedcba9876543210", 0, 0,
                                        background_base64.c_str()));
    assert(music_data_background_available());
    assert(music_data_background_size() == background_bytes.size());
    assert(strcmp(music_data_background_id(), "fedcba9876543210") == 0);
    const uint32_t background_revision = music_data_background_revision();
    const uint8_t *background_pointer = music_data_background_data();
    music_data_mark_disconnected();
    assert(music_data_background_available());
    assert(music_data_background_revision() == background_revision);
    assert(music_data_background_data() == background_pointer);
    assert(!music_data_begin_background("0123456789abcdef",
                                        background_bytes.size(), 1,
                                        background_crc));
    assert(!music_data_begin_background("fedcba9876543210",
                                        MUSIC_BACKGROUND_MAX_BYTES + 1, 1, crc));
    assert(music_data_begin_background("fedcba9876543210",
                                       background_bytes.size(), 1,
                                       background_crc));
    assert(!music_data_append_background("fedcba9876543210", 1, 0,
                                         background_base64.c_str()));
    assert(!music_data_append_background("fedcba9876543210", 0, 0,
                                         background_base64.c_str()));
    assert(music_data_background_revision() == background_revision);
    assert(music_data_begin_background("fedcba9876543210",
                                       background_bytes.size(), 1,
                                       background_crc ^ 1));
    assert(!music_data_append_background("fedcba9876543210", 0, 0,
                                         background_base64.c_str()));
    assert(music_data_background_revision() == background_revision);
    const auto wrong_dimensions = jfif(640, 480);
    const std::string wrong_dimensions_base64 = base64(wrong_dimensions);
    assert(music_data_begin_background(
        "fedcba9876543210", wrong_dimensions.size(), 1,
        music_crc32(wrong_dimensions.data(), wrong_dimensions.size())));
    assert(!music_data_append_background("fedcba9876543210", 0, 0,
                                         wrong_dimensions_base64.c_str()));
    assert(music_data_background_revision() == background_revision);
    assert(music_data_begin_background("fedcba9876543210",
                                       background_bytes.size(), 1,
                                       background_crc));
    assert(music_data_append_background("fedcba9876543210", 0, 0,
                                        background_base64.c_str()));
    assert(music_data_background_revision() == background_revision + 1);
    assert(music_data_background_data() != background_pointer);
    // The former active allocation remains pinned until the UI explicitly
    // drops its LVGL cache/descriptor reference.
    assert(background_pointer[0] == 0xFF);
    assert(!music_data_begin_background("fedcba9876543210",
                                        background_bytes.size(), 1,
                                        background_crc));
    music_data_release_retired_background();
    assert(music_data_begin_background("fedcba9876543210",
                                       background_bytes.size(), 1,
                                       background_crc));

    MusicVisualizerState visualizer{};
    visualizer.sequence = 4;
    visualizer.enabled = true;
    visualizer.available = true;
    for (uint8_t i = 0; i < MUSIC_VISUALIZER_BANDS; ++i) {
        visualizer.bands[i] = static_cast<uint8_t>(i * 15);
    }
    assert(music_data_ingest_visualizer(visualizer, 100));
    assert(music_data_visualizer_status(850) ==
           MusicVisualizerStatus::VISUALIZER_LIVE);
    assert(music_data_visualizer_status(851) ==
           MusicVisualizerStatus::VISUALIZER_STALE);
    MusicVisualizerState visualizer_out{};
    assert(music_data_get_visualizer(visualizer_out));
    assert(visualizer_out.sequence == 4);
    assert(visualizer_out.bands[15] == 225);

    visualizer.available = false;
    assert(music_data_ingest_visualizer(visualizer, 900));
    assert(music_data_visualizer_status(900) ==
           MusicVisualizerStatus::VISUALIZER_UNAVAILABLE);
    assert(music_data_get_visualizer(visualizer_out));
    assert(visualizer_out.bands[15] == 0);
    visualizer.enabled = false;
    assert(music_data_ingest_visualizer(visualizer, 901));
    assert(music_data_visualizer_status(901) ==
           MusicVisualizerStatus::VISUALIZER_DISABLED);
    visualizer.available = true;
    assert(!music_data_ingest_visualizer(visualizer, 902));

    puts("music_data_native: PASS");
    return 0;
}
