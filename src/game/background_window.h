#pragma once

namespace spass::game {

// Test runs in the background (config.toml [debug] background = true, written by scripts/test-run.ps1 unless -AllowFocus):
// the game never takes the foreground from the user and leaves the mouse cursor alone while another program is in front.
// Hooks user32 in this process: ShowWindow/ShowWindowAsync/CreateWindowEx(WS_VISIBLE)/SetWindowPos of the process's own
// top-level windows become non-activating, SetForegroundWindow/BringWindowToTop/SetActiveWindow on them do nothing, and
// SetCursorPos / ClipCursor(rect) are dropped while the foreground window belongs to another process (eCGuiModule2::SetFocusPos,
// eCApplication::ConfineCursor; RE.md section 57). Once the user clicks into the game window everything behaves as usual.
// Call before enableAll().
bool installBackgroundWindow();

}  // namespace spass::game
