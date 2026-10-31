#ifndef UI_PERFORMANCE_H
#define UI_PERFORMANCE_H

#include <lvgl.h>

// EchoDesk Performance Centre navigation is owned by the shell.  The module
// calls Home from every view and Back only when Back is pressed on the view
// selector.  Back inside a metric view returns to the selector locally.
using UiPerformanceNavigationCallback = void (*)(void *context);

void ui_performance_set_navigation_callbacks(UiPerformanceNavigationCallback home,
                                             UiPerformanceNavigationCallback back,
                                             void *context = nullptr);

// Creates (or returns) the module-owned 320x240 LVGL screen.  The caller is
// responsible for loading it with the desired shell transition.
lv_obj_t *ui_performance_create();

// Refreshes current values, history, freshness and thermal pressure.  Safe to
// call from the normal EchoDesk tick; it is a no-op while the screen is absent
// or inactive.
void ui_performance_tick();

// Rebuilds the screen with current theme colours and returns the replacement.
// If the old screen was active it is replaced without an animation.
lv_obj_t *ui_performance_recreate();

// Returns the currently owned screen, or nullptr after LVGL has deleted it.
lv_obj_t *ui_performance_screen();

#endif // UI_PERFORMANCE_H
