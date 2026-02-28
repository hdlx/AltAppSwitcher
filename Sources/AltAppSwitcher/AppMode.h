#include <windef.h>
struct Config;
HWND AppModeCreateWindow();
void AppModeInit(HINSTANCE instance, const struct Config* cfg);
void AppModeDeinit();
void AppModeDestroyWindow(HWND window);
void AppModeCloseApp(HWND window);