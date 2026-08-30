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

// Most-recently-used window tracking.
// EnumWindows() only reflects raw Z-order, and Windows sends minimized
// windows to the bottom of the Z-order, which is why they used to jump
// to the end of the switcher list on minimize. This tracks real
// activation order (via EVENT_SYSTEM_FOREGROUND) independently of Z-order
// and of minimize/restore, so callers can sort the switcher list by
// "last used" instead of raw Z-order.
void MruTrackerInit(void);
void MruTrackerDeinit(void);
// Returns the 0-based recency rank of hwnd (0 = most recently used), or
// -1 if hwnd has not been observed becoming foreground yet.
int MruTrackerGetRank(HWND hwnd);
