#ifndef UI_ECHODESK_H
#define UI_ECHODESK_H

// Freenove-only EchoDesk OS shell. The implementation is compiled only for
// the dedicated EchoDesk target; legacy board UIs keep their existing path.
void ui_echodesk_init();
void ui_echodesk_tick();
void ui_echodesk_recreate();

#endif // UI_ECHODESK_H
