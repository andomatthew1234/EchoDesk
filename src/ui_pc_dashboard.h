#ifndef UI_PC_DASHBOARD_H
#define UI_PC_DASHBOARD_H

#include <lvgl.h>

using UiPcDashboardNavigationCallback = void (*)(void *context);

void ui_pc_dashboard_set_navigation_callback(
    UiPcDashboardNavigationCallback callback, void *context = nullptr);
lv_obj_t *ui_pc_dashboard_create();
lv_obj_t *ui_pc_dashboard_screen();
lv_obj_t *ui_pc_dashboard_recreate();
void ui_pc_dashboard_tick();

#endif // UI_PC_DASHBOARD_H
