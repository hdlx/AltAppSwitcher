#include <stdbool.h>
#include <windef.h>
struct Config;
typedef struct IVirtualDesktopManager IVirtualDesktopManager;
bool IsEligibleWindow(HWND hwnd, const struct Config* cfg, HMONITOR mouseMonitor, bool ignoreMinimizedWindows, IVirtualDesktopManager* vdm);
int Modulo(int a, int b);
void CommonInit(HINSTANCE instance);
void CommonDeinit(HINSTANCE instance);
void ApplyWithTimeout(void (*fn)(void*), void* data, HINSTANCE instance);
DWORD TryAttachToForeground();
unsigned int USKeyToLocalKey(unsigned int keyCode);