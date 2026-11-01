#ifndef UI_MY_DEVICE_H
#define UI_MY_DEVICE_H

#include <lvgl.h>

using UiMyDeviceNavigationCallback = void (*)(void *context);

void ui_my_device_set_navigation_callback(UiMyDeviceNavigationCallback callback,
                                          void *context = nullptr);
lv_obj_t *ui_my_device_create();
lv_obj_t *ui_my_device_screen();
lv_obj_t *ui_my_device_recreate();
void ui_my_device_tick();

#endif // UI_MY_DEVICE_H
