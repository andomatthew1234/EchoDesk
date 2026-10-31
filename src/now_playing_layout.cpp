#include "now_playing_layout.h"

#include "config.h"

namespace {

constexpr NowPlayingLayoutGeometry LAYOUTS[NOW_PLAYING_LAYOUT_COUNT] = {
    // Album Cover: centred hero artwork, minimal text and bottom controls.
    {{106, 34, 108, 108}, {16, 147, 288, 20}, {16, 169, 288, 16},
     {0, 0, 0, 0}, {16, 188, 288, 3}, {72, 194, 48, 42},
     {136, 194, 48, 42}, {200, 194, 48, 42}, 14, true, false},
    // CarPlay Inspired: artwork left, metadata and controls right.
    {{8, 48, 126, 126}, {148, 49, 164, 42}, {148, 96, 164, 18},
     {148, 119, 164, 18}, {148, 153, 160, 4}, {148, 187, 46, 44},
     {204, 187, 46, 44}, {260, 187, 46, 44}, 14, true, false},
    // EchoDesk: square artwork, brand brackets and solid theme background.
    {{14, 62, 112, 112}, {145, 58, 165, 42}, {145, 105, 165, 18},
     {145, 128, 165, 18}, {145, 151, 165, 4}, {148, 183, 46, 44},
     {204, 183, 46, 44}, {260, 183, 46, 44}, 0, false, true},
};

} // namespace

uint8_t now_playing_layout_normalize(uint8_t layout) {
    return layout < NOW_PLAYING_LAYOUT_COUNT ? layout : 0;
}

uint8_t now_playing_background_normalize(uint8_t background) {
    return background < NOW_PLAYING_BACKGROUND_COUNT ? background
                                                      : NOW_PLAYING_BACKGROUND_DEFAULT;
}

uint8_t now_playing_default_background(uint8_t layout) {
    return now_playing_layout_normalize(layout) == NOW_PLAYING_LAYOUT_ECHODESK
        ? NOW_PLAYING_BACKGROUND_ECHODESK
        : NOW_PLAYING_BACKGROUND_ALBUM_COVER;
}

uint8_t now_playing_resolve_background(uint8_t layout, uint8_t background,
                                       bool customized) {
    return customized ? now_playing_background_normalize(background)
                      : now_playing_default_background(layout);
}

const NowPlayingLayoutGeometry &now_playing_layout_geometry(uint8_t layout) {
    return LAYOUTS[now_playing_layout_normalize(layout)];
}

bool now_playing_rect_within_screen(const NowPlayingRect &rect) {
    if (rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0) return false;
    return rect.x + rect.width <= 320 && rect.y + rect.height <= 240;
}

bool now_playing_rects_overlap(const NowPlayingRect &left,
                               const NowPlayingRect &right) {
    if (left.width <= 0 || left.height <= 0 ||
        right.width <= 0 || right.height <= 0) return false;
    return left.x < right.x + right.width && left.x + left.width > right.x &&
           left.y < right.y + right.height && left.y + left.height > right.y;
}

uint16_t now_playing_progress_per_mille(uint32_t position_ms,
                                       uint32_t duration_ms) {
    if (duration_ms == 0) return 0;
    if (position_ms >= duration_ms) return 1000;
    return static_cast<uint16_t>((static_cast<uint64_t>(position_ms) * 1000ULL) /
                                 duration_ms);
}

int8_t now_playing_corner_offset(uint32_t now_ms, bool playing) {
    if (!playing) return 0;
    // A restrained 1.6-second triangular expansion: 0, 1, 2, 1, 0.
    const uint16_t phase = static_cast<uint16_t>(now_ms % 1600U);
    const uint16_t folded = phase <= 800U ? phase : 1600U - phase;
    return static_cast<int8_t>((folded + 200U) / 400U);
}

uint8_t now_playing_smooth_audio_band(uint8_t current, uint8_t target) {
    if (target > current) {
        return static_cast<uint8_t>(current + (target - current + 1U) / 2U);
    }
    if (current > target) {
        return static_cast<uint8_t>(current - (current - target + 7U) / 8U);
    }
    return current;
}

uint8_t now_playing_decay_audio_peak(uint8_t peak, uint8_t level) {
    if (level >= peak) return level;
    return peak > 2 ? static_cast<uint8_t>(peak - 2) : 0;
}
