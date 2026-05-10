#pragma once

union SDL_Event;
struct interface_of_controller_pad;

namespace dusk::touch_controls {

// Process a raw SDL event (call from the Aurora event loop on AURORA_SDL_EVENT).
void handle_event(const SDL_Event& event);

// Render the virtual gamepad overlay (call from ImGuiConsole::PostDraw).
void draw();

// OR virtual button state into pad port 0 after mDoCPd_c::read() fills it.
void apply_virtual_input(interface_of_controller_pad* pad);

bool is_enabled();

// Enter button-repositioning mode (same as tapping the in-game "Edit" button).
// Closes the settings UI first; has no effect if touch controls are disabled.
void enter_customize_mode();

// Call when a physical controller connects/disconnects to auto-toggle the overlay.
void notify_controller_added();
void notify_controller_removed();

} // namespace dusk::touch_controls
