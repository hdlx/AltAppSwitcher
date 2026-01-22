#include <stdbool.h>
#include <windef.h>
struct Config;
bool IsEligibleWindow(HWND hwnd, const struct Config* cfg, HMONITOR mouseMonitor, bool ignoreMinimizedWindows);
int Modulo(int a, int b);
void CommonInit(HINSTANCE instance);
void CommonDeinit(HINSTANCE instance);
void ApplyWithTimeout(void (*fn)(void*), void* data, HINSTANCE instance, HWND parentWin);