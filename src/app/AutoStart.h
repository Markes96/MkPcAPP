#pragma once

namespace app {

// Manages the HKCU\...\Run autostart entry. User-level (HKCU) by design — no
// elevation required just to toggle "start with Windows".
namespace AutoStart {

bool IsEnabled();
void SetEnabled(bool enabled);

} // namespace AutoStart

} // namespace app
