#include <windef.h>
struct Config;
typedef struct IVirtualDesktopManager IVirtualDesktopManager;
HWND WinModeCreateWindow();
void WinModeInit(HINSTANCE instance, const struct Config* cfg, IVirtualDesktopManager* VDM);
void WinModeDeinit();
void WinModeDestroyWindow(HWND window);
// Selects and focuses the next (direction 1) or previous (-1) window.
void WinModeNavigate(HWND window, int direction);