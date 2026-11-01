#ifndef ECHODESK_STATUS_COPY_H
#define ECHODESK_STATUS_COPY_H

#include "music_data.h"
#include "performance_data.h"

const char *echodesk_music_status_text(MusicConnectionState connection,
                                       bool empty,
                                       MusicPlaybackState playback);
const char *echodesk_music_empty_title(MusicConnectionState connection);
const char *echodesk_music_empty_detail(MusicConnectionState connection);
const char *echodesk_performance_status_text(
    PerformanceConnectionState connection);

#endif // ECHODESK_STATUS_COPY_H
