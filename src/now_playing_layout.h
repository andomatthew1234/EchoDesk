#ifndef NOW_PLAYING_LAYOUT_H
#define NOW_PLAYING_LAYOUT_H

#include <stdint.h>

constexpr uint8_t NOW_PLAYING_LAYOUT_COUNT = 3;
constexpr uint8_t NOW_PLAYING_BACKGROUND_COUNT = 3;

struct NowPlayingRect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

struct NowPlayingLayoutGeometry {
    NowPlayingRect artwork;
    NowPlayingRect title;
    NowPlayingRect artist;
    NowPlayingRect album;
    NowPlayingRect progress;
    NowPlayingRect previous;
    NowPlayingRect play_pause;
    NowPlayingRect next;
    uint8_t artwork_radius;
    bool artwork_background;
    bool animated_corners;
};

uint8_t now_playing_layout_normalize(uint8_t layout);
uint8_t now_playing_background_normalize(uint8_t background);
uint8_t now_playing_default_background(uint8_t layout);
uint8_t now_playing_resolve_background(uint8_t layout, uint8_t background,
                                       bool customized);
const NowPlayingLayoutGeometry &now_playing_layout_geometry(uint8_t layout);
bool now_playing_rect_within_screen(const NowPlayingRect &rect);
bool now_playing_rects_overlap(const NowPlayingRect &left,
                               const NowPlayingRect &right);
uint16_t now_playing_progress_per_mille(uint32_t position_ms,
                                       uint32_t duration_ms);
int8_t now_playing_corner_offset(uint32_t now_ms, bool playing);
uint8_t now_playing_smooth_audio_band(uint8_t current, uint8_t target);
uint8_t now_playing_decay_audio_peak(uint8_t peak, uint8_t level);

#endif // NOW_PLAYING_LAYOUT_H
