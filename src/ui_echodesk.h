#ifndef UI_ECHODESK_H
#define UI_ECHODESK_H

// Freenove-only EchoDesk OS shell. The implementation is compiled only for
// the dedicated EchoDesk target; legacy board UIs keep their existing path.
void ui_echodesk_init();
void ui_echodesk_tick();
void ui_echodesk_recreate();

// EchoDesk Desktop control surface. Navigation is queued and applied from the
// normal UI tick, never re-entrantly inside the serial parser.
bool ui_echodesk_request_open_app(const char *app);
const char *ui_echodesk_active_app_name();
void ui_echodesk_request_restart();

// Queues an onboarding screen transition after the serial/config layer has
// persisted a new setup state. Applied from ui_echodesk_tick(), never from the
// serial parser's call stack.
void ui_echodesk_setup_state_changed();

#endif // UI_ECHODESK_H
