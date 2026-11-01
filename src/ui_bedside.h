#ifndef UI_BEDSIDE_H
#define UI_BEDSIDE_H

#include <lvgl.h>

using UiBedsideNavigationCallback = void (*)(void *context);

void ui_bedside_set_navigation_callback(UiBedsideNavigationCallback callback,
                                        void *context = nullptr);
lv_obj_t *ui_bedside_create();
lv_obj_t *ui_bedside_screen();
lv_obj_t *ui_bedside_recreate();
void ui_bedside_tick();
void ui_bedside_leave();

#endif // UI_BEDSIDE_H
