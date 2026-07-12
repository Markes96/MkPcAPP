#pragma once
#include <windows.h>

namespace profiles {
namespace ScreenControl {

// "Apagar pantalla ahora" — the same signal Windows sends itself when the
// inactivity timeout expires. Doesn't lock the session or sleep the machine.
// Stateless and can't meaningfully fail in a way the UI would show.
//
// Sent to `hwnd` (the app's own window) rather than HWND_BROADCAST: this is
// a system-wide power action handled by DefWindowProc regardless of which
// window receives it, so broadcasting isn't needed -- and SendMessage to
// HWND_BROADCAST blocks the calling thread until EVERY top-level window on
// the whole desktop (any process) has processed the message, which froze
// this app (shown as "Not Responding") whenever some unrelated window was
// slow to respond.
void TurnOffScreenNow(HWND hwnd);

} // namespace ScreenControl
} // namespace profiles
