#include <windef.h>
struct Config;
HWND WinModeCreateWindow();
void WinModeInit(HINSTANCE instance, const struct Config* cfg);
void WinModeDeinit();
void WinModeDestroyWindow(HWND window);