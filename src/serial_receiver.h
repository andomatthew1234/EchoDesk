#ifndef SERIAL_RECEIVER_H
#define SERIAL_RECEIVER_H

#include "api_common.h"

// Initialize serial receiver state
void serial_receiver_init();

// Call from loop() — reads Serial, parses complete JSON lines
void serial_receiver_tick();

// Get a copy of the current monitor state
MonitorState serial_get_state();

// Get the most recent cached state for a specific provider without changing
// the companion-managed active view. EchoDesk uses this to keep its Codex app
// bound to live Codex data while its own Home/Clock/Settings navigation is open.
bool serial_get_provider_state(uint8_t provider, MonitorState &out);

// Returns true if we received valid data within the last 5 minutes
bool serial_has_recent_data();

// True while the companion has sent a valid usage or notice frame recently.
bool serial_has_recent_host_frame();

// Returns true if new data arrived since last call (auto-resets)
bool serial_has_new_data();

// Display-Fenster: Auswahl per Touch oder Timer; die Konfiguration kommt vom Host.
void serial_select_view(uint8_t index);
void serial_next_view();
void serial_previous_view();
bool serial_is_clock_view();
uint8_t serial_view_count();
bool serial_is_plugin_view();
uint8_t serial_active_view();

// Returns current display time string ("HH:MM" or "--:--")
const char* serial_get_display_time();

// Applies EchoDesk's persisted named timezone to newlib's POSIX TZ runtime.
// Companion UTC timestamps keep setting the clock, but do not overwrite this
// preference with a transient fixed offset.
void serial_apply_configured_timezone();

// C4: Deferred UI rebuild. The parser requests a rebuild (set_theme /
// set_language / set_orientation); main loop() performs it after the tick.
void serial_request_ui_rebuild();
bool serial_ui_rebuild_requested();
void serial_ui_rebuild_clear();

// Marks the versioned EchoDesk control snapshot dirty after a touchscreen or
// legacy-command change. The snapshot is emitted from serial_receiver_tick().
void serial_echodesk_state_changed();

// C3: True once the Mac companion sent a TZ offset. wifi_time uses this to
// avoid overwriting the host-selected timezone with its fixed NTP TZ.
bool host_timezone_is_set();

#endif // SERIAL_RECEIVER_H
