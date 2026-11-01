#include "echodesk_status_copy.h"

const char *echodesk_music_status_text(MusicConnectionState connection,
                                       bool empty,
                                       MusicPlaybackState playback) {
    if (connection == MusicConnectionState::NEVER_RECEIVED) return "WAITING";
    if (connection == MusicConnectionState::DISCONNECTED) return "OFFLINE";
    if (connection == MusicConnectionState::STALE) return "STALE";
    if (empty) return "READY";
    if (playback == MusicPlaybackState::PLAYING) return "PLAYING";
    if (playback == MusicPlaybackState::PAUSED) return "PAUSED";
    return "STOPPED";
}

const char *echodesk_music_empty_title(MusicConnectionState connection) {
    if (connection == MusicConnectionState::NEVER_RECEIVED) {
        return "WAITING FOR MUSIC";
    }
    if (connection == MusicConnectionState::DISCONNECTED) {
        return "MUSIC UNAVAILABLE";
    }
    return "NOTHING PLAYING";
}

const char *echodesk_music_empty_detail(MusicConnectionState connection) {
    if (connection == MusicConnectionState::NEVER_RECEIVED) {
        return "Waiting for EchoDesk Desktop";
    }
    if (connection == MusicConnectionState::DISCONNECTED) {
        return "Reconnect EchoDesk Desktop";
    }
    if (connection == MusicConnectionState::STALE) {
        return "Music data is delayed";
    }
    return "Start playback on your computer";
}

const char *echodesk_performance_status_text(
    PerformanceConnectionState connection) {
    switch (connection) {
        case PerformanceConnectionState::LIVE: return "LIVE";
        case PerformanceConnectionState::STALE: return "STALE";
        case PerformanceConnectionState::DISCONNECTED: return "OFFLINE";
        case PerformanceConnectionState::NEVER_RECEIVED: return "WAITING";
    }
    return "WAITING";
}
