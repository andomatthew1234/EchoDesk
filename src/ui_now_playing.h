#ifndef UI_NOW_PLAYING_H
#define UI_NOW_PLAYING_H

#include <lvgl.h>

typedef void (*UiNowPlayingNavigationCallback)(void *context);

void ui_now_playing_set_navigation_callback(UiNowPlayingNavigationCallback callback,
                                            void *context = nullptr);
lv_obj_t *ui_now_playing_create();
lv_obj_t *ui_now_playing_screen();
lv_obj_t *ui_now_playing_recreate();
void ui_now_playing_tick();
void ui_now_playing_leave();

#endif // UI_NOW_PLAYING_H
