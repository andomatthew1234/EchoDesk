#include "../src/now_playing_layout.h"

#include <assert.h>
#include <stdio.h>

int main() {
    assert(now_playing_layout_normalize(0) == 0);
    assert(now_playing_layout_normalize(1) == 1);
    assert(now_playing_layout_normalize(2) == 2);
    assert(now_playing_layout_normalize(255) == 0);
    assert(now_playing_background_normalize(0) == 0);
    assert(now_playing_background_normalize(1) == 1);
    assert(now_playing_background_normalize(2) == 2);
    assert(now_playing_background_normalize(255) == 0);
    assert(now_playing_default_background(0) == 0);
    assert(now_playing_default_background(1) == 0);
    assert(now_playing_default_background(2) == 1);
    for (uint8_t layout = 0; layout < NOW_PLAYING_LAYOUT_COUNT; ++layout) {
        for (uint8_t background = 0; background < NOW_PLAYING_BACKGROUND_COUNT;
             ++background) {
            assert(now_playing_resolve_background(layout, background, true) == background);
        }
        assert(now_playing_resolve_background(layout, 2, false) ==
               now_playing_default_background(layout));
    }

    const NowPlayingRect home{4, 4, 68, 36};
    const NowPlayingRect customize{270, 4, 46, 36};
    assert(now_playing_rect_within_screen(home));
    assert(now_playing_rect_within_screen(customize));

    for (uint8_t layout = 0; layout < NOW_PLAYING_LAYOUT_COUNT; ++layout) {
        const auto &geometry = now_playing_layout_geometry(layout);
        assert(now_playing_rect_within_screen(geometry.artwork));
        assert(now_playing_rect_within_screen(geometry.title));
        assert(now_playing_rect_within_screen(geometry.artist));
        assert(now_playing_rect_within_screen(geometry.progress));
        assert(now_playing_rect_within_screen(geometry.previous));
        assert(now_playing_rect_within_screen(geometry.play_pause));
        assert(now_playing_rect_within_screen(geometry.next));
        if (geometry.album.width > 0) {
            assert(now_playing_rect_within_screen(geometry.album));
        }
        assert(geometry.previous.width >= 46 && geometry.previous.height >= 42);
        assert(geometry.play_pause.width >= 46 && geometry.play_pause.height >= 42);
        assert(geometry.next.width >= 46 && geometry.next.height >= 42);
        assert(!now_playing_rects_overlap(geometry.artwork, geometry.title));
        assert(!now_playing_rects_overlap(geometry.title, geometry.artist));
        assert(!now_playing_rects_overlap(geometry.progress, geometry.previous));
    }

    assert(now_playing_layout_geometry(0).artwork_radius > 0);
    assert(now_playing_layout_geometry(0).artwork_background);
    assert(now_playing_layout_geometry(1).artwork_radius > 0);
    assert(now_playing_layout_geometry(1).artwork_background);
    assert(now_playing_layout_geometry(2).artwork_radius == 0);
    assert(!now_playing_layout_geometry(2).artwork_background);
    assert(now_playing_layout_geometry(2).animated_corners);

    assert(now_playing_progress_per_mille(0, 0) == 0);
    assert(now_playing_progress_per_mille(500, 1000) == 500);
    assert(now_playing_progress_per_mille(1500, 1000) == 1000);
    assert(now_playing_corner_offset(0, false) == 0);
    assert(now_playing_corner_offset(400, true) == 1);
    assert(now_playing_corner_offset(800, true) == 2);
    assert(now_playing_corner_offset(1200, true) == 1);
    assert(now_playing_corner_offset(1600, true) == 0);
    assert(now_playing_smooth_audio_band(0, 200) == 100);
    assert(now_playing_smooth_audio_band(100, 200) == 150);
    assert(now_playing_smooth_audio_band(200, 0) == 175);
    assert(now_playing_smooth_audio_band(40, 40) == 40);
    assert(now_playing_decay_audio_peak(100, 120) == 120);
    assert(now_playing_decay_audio_peak(100, 20) == 98);
    assert(now_playing_decay_audio_peak(2, 0) == 0);

    puts("now_playing_layout_native: PASS");
    return 0;
}
