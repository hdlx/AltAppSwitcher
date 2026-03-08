#include <windef.h>
struct Config;
typedef struct IVirtualDesktopManager IVirtualDesktopManager;
HWND WinModeCreateWindow();
void WinModeInit(HINSTANCE instance, const struct Config* cfg, IVirtualDesktopManager* VDM);
void WinModeDeinit();
void WinModeDestroyWindow(HWND window);