#include <stdbool.h>
#include <windef.h>
struct Config;
bool IsEligibleWindow(HWND hwnd, const struct Config* cfg, HMONITOR mouseMonitor, bool ignoreMinimizedWindows);
