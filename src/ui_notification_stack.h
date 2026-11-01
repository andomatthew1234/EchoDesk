#ifndef UI_NOTIFICATION_STACK_H
#define UI_NOTIFICATION_STACK_H

#include <lvgl.h>

using UiNotificationStackNavigationCallback = void (*)(void *context);

void ui_notification_stack_set_navigation_callback(
    UiNotificationStackNavigationCallback callback, void *context = nullptr);
lv_obj_t *ui_notification_stack_create();
lv_obj_t *ui_notification_stack_screen();
lv_obj_t *ui_notification_stack_recreate();
void ui_notification_stack_tick();

#endif // UI_NOTIFICATION_STACK_H
