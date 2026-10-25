#include <stdbool.h>
#include <windef.h>
struct Config;
typedef struct IVirtualDesktopManager IVirtualDesktopManager;
HWND AppModeCreateWindow();
void AppModeInit(HINSTANCE instance, const struct Config* cfg, IVirtualDesktopManager* VDM);
void AppModeDeinit();
void AppModeDestroyWindow(HWND window);
void AppModeRefreshGraphicsResources();
// True once after the switcher closed itself (mouse click, escape) while
// the hold key may still be down. Called from the keyboard hook thread.
bool AppModeTakeSelfClosed(void);