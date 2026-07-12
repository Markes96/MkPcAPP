#include "ScreenControl.h"
#include <windows.h>

namespace profiles {
namespace ScreenControl {

void TurnOffScreenNow(HWND hwnd) {
    SendMessageW(hwnd, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
}

} // namespace ScreenControl
} // namespace profiles
