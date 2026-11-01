#ifndef UI_CLOCK_H
#define UI_CLOCK_H

#include "echodesk_clock.h"

#include <lvgl.h>

using UiClockNavigationCallback = void (*)(void *context);
using UiClockConfigurationRequestCallback = bool (*)(EchoDeskClockMode mode,
                                                      void *context);

void ui_clock_set_callbacks(UiClockNavigationCallback home_callback,
                            UiClockConfigurationRequestCallback config_callback,
                            UiClockNavigationCallback state_changed_callback,
                            void *context = nullptr);

lv_obj_t *ui_clock_create();
lv_obj_t *ui_clock_screen();
lv_obj_t *ui_clock_recreate();
void ui_clock_tick();

#endif // UI_CLOCK_H
