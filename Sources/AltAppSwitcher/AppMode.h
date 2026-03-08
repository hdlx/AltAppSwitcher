#include <windef.h>
struct Config;
typedef struct IVirtualDesktopManager IVirtualDesktopManager;
HWND AppModeCreateWindow();
void AppModeInit(HINSTANCE instance, const struct Config* cfg, IVirtualDesktopManager* VDM);
void AppModeDeinit();
void AppModeDestroyWindow(HWND window);