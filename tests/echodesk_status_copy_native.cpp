#include "../src/echodesk_status_copy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main() {
    assert(strcmp(echodesk_music_status_text(
                      MusicConnectionState::NEVER_RECEIVED, true,
                      MusicPlaybackState::STOPPED),
                  "WAITING") == 0);
    assert(strcmp(echodesk_music_empty_title(
                      MusicConnectionState::NEVER_RECEIVED),
                  "WAITING FOR MUSIC") == 0);
    assert(strcmp(echodesk_music_empty_detail(
                      MusicConnectionState::NEVER_RECEIVED),
                  "Waiting for EchoDesk Desktop") == 0);
    assert(strcmp(echodesk_music_empty_detail(
                      MusicConnectionState::DISCONNECTED),
                  "Reconnect EchoDesk Desktop") == 0);
    assert(strcmp(echodesk_music_empty_detail(MusicConnectionState::LIVE),
                  "Start playback on your computer") == 0);
    assert(strcmp(echodesk_music_status_text(
                      MusicConnectionState::LIVE, false,
                      MusicPlaybackState::PLAYING),
                  "PLAYING") == 0);

    assert(strcmp(echodesk_performance_status_text(
                      PerformanceConnectionState::NEVER_RECEIVED),
                  "WAITING") == 0);
    assert(strcmp(echodesk_performance_status_text(
                      PerformanceConnectionState::DISCONNECTED),
                  "OFFLINE") == 0);
    assert(strcmp(echodesk_performance_status_text(
                      PerformanceConnectionState::STALE),
                  "STALE") == 0);
    assert(strcmp(echodesk_performance_status_text(
                      PerformanceConnectionState::LIVE),
                  "LIVE") == 0);

    puts("echodesk_status_copy_native: PASS");
    return 0;
}
