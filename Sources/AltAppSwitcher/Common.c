#include "Common.h"
#define COBJMACROS
#include <string.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winnt.h>
#include <winscard.h>
#include <processthreadsapi.h>
#include <gdiplus.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <windowsx.h>
#include <unistd.h>
// https://stackoverflow.com/questions/71437203/proper-way-of-activating-a-window-using-winapi
#include <Initguid.h>
#include <uiautomationclient.h>
#include <gdiplus/gdiplusenums.h>
#include <PropKey.h>
#include <Shobjidl.h>
#include "AppxPackaging.h"
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Messages.h"

static const char* WindowsClassNamesToSkip[] = {
    "Shell_TrayWnd",
    "DV2ControlHost",
    "MsgrIMEWindowClass",
    "SysShadow",
    "Button",
    "Windows.UI.Core.CoreWindow",
    "Dwm"
};

static bool BelongsToCurrentDesktop(HWND window)
{
    IVirtualDesktopManager* vdm = NULL;
    (void)vdm;
    CoInitialize(NULL);
    CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL, &IID_IVirtualDesktopManager, (void**)&vdm);

    if (!vdm) {
        CoUninitialize();
        return true;
    }

    WINBOOL isCurrent = true;
    IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(vdm, window, &isCurrent);
    IVirtualDesktopManager_Release(vdm);
    CoUninitialize();
    return isCurrent;
}

static bool IsWindowOnMonitor(HWND hwnd, HMONITOR targetMonitor)
{
    RECT windowRect;

    // For minimized windows, use the restored position instead of current position
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp)) {
            windowRect = wp.rcNormalPosition;
        } else {
            // Fallback to GetWindowRect if GetWindowPlacement fails
            if (!GetWindowRect(hwnd, &windowRect))
                return false;
        }
    } else {
        if (!GetWindowRect(hwnd, &windowRect))
            return false;
    }

    // Use MonitorFromRect for more accurate monitor detection
    // This considers the window's entire area, not just center point
    HMONITOR windowMonitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);

    // Use CompareObjectHandles if available (Windows 10+) for more robust comparison
    // Fall back to direct comparison for older Windows versions
    static HMODULE kernel32 = NULL;
    static BOOL(WINAPI * pCompareObjectHandles)(HANDLE, HANDLE) = NULL;
    static bool initialized = false;

    if (!initialized) {
        kernel32 = GetModuleHandleA("kernel32.dll");
        if (kernel32) {
            pCompareObjectHandles = (BOOL(WINAPI*)(HANDLE, HANDLE))
                GetProcAddress(kernel32, "CompareObjectHandles");
        }
        initialized = true;
    }

    if (pCompareObjectHandles) {
        return pCompareObjectHandles(windowMonitor, targetMonitor);
    }

    // Fallback to direct comparison for older Windows versions
    return windowMonitor == targetMonitor;
}

int Modulo(int a, int b)
{
    if (b == 0)
        return 0;
    return (a % b + b) % b;
}

bool IsEligibleWindow(HWND hwnd, const struct Config* cfg, HMONITOR mouseMonitor, bool ignoreMinimizedWindows)
{
    if (hwnd == GetShellWindow()) // Desktop
        return false;
    WINDOWINFO wi = {};
    wi.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(hwnd, &wi);
    if (!(wi.dwStyle & WS_VISIBLE))
        return false;
    // Chrome has sometime WS_EX_TOOLWINDOW while beeing an alttabable window
    if ((wi.dwExStyle & WS_EX_TOOLWINDOW))
        return false;
    if ((wi.dwExStyle & WS_EX_TOPMOST) && !(wi.dwExStyle & WS_EX_APPWINDOW))
        return false;

    // Start at the root owner
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    (void)owner;
    // const HWND parent = GetAncestor(hwnd, GA_PARENT); (void)parent;
    // const HWND dw = GetDesktopWindow(); (void)dw;
    // Taskbar window if: owner is self or WS_EX_APPWINDOW is set
    bool b = (wi.dwExStyle & WS_EX_APPWINDOW) != 0;
    (void)(b);
    bool isOwned = owner != hwnd && owner != NULL;
    if ((isOwned) && !(wi.dwExStyle & WS_EX_APPWINDOW))
        return false;

    if (cfg->DesktopFilter == DesktopFilterCurrent && !BelongsToCurrentDesktop(hwnd))
        return false;

    if (!IsWindowVisible(hwnd))
        return false;

    static char buf[512];
    GetClassName(hwnd, buf, 512);
    for (uint32_t i = 0; i < sizeof(WindowsClassNamesToSkip) / sizeof(WindowsClassNamesToSkip[0]); i++) {
        if (!strcmp(WindowsClassNamesToSkip[i], buf))
            return false;
    }
    WINBOOL cloaked = false;
    if (!strcmp(buf, "ApplicationFrameWindow"))
        DwmGetWindowAttribute(hwnd, (DWORD)DWMWA_CLOAKED, (PVOID)&cloaked, (DWORD)sizeof(cloaked));
    if (cloaked)
        return false;

    // Filter apps by monitor if enabled
    if (cfg->AppFilterMode == AppFilterModeMouseMonitor) {
        if (!IsWindowOnMonitor(hwnd, mouseMonitor))
            return true;
    }

    if (ignoreMinimizedWindows) {
        WINDOWPLACEMENT placement;
        placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &placement);
        if (placement.showCmd == SW_SHOWMINIMIZED)
            return false;
    }

    return true;
}

struct WorkerArg {
    void (*fn)(void*);
    void* data;
    HANDLE workerReady;
    HINSTANCE instance;
    HWND workerWin;
};

static LRESULT WorkerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WorkerArg* wa = NULL;
    switch (uMsg) {
    case WM_CREATE: {
        wa = (struct WorkerArg*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        return 0;
    }
    case WM_CLOSE: {
        ASSERT(wa);
        ASSERT(wa->fn);
        wa->fn(wa->data);
        DestroyWindow(hwnd);
        return 0;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    default: {
        break;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static char WorkerClassName[] = "AASWorker";

void CommonInit(HINSTANCE instance)
{
    WNDCLASS wc = {};
    wc.lpfnWndProc = WorkerWindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = WorkerClassName;
    wc.cbWndExtra = sizeof(void*);
    wc.style = 0;
    wc.hbrBackground = NULL;
    ASSERT(RegisterClass(&wc));
}

void CommonDeinit(HINSTANCE instance)
{
    UnregisterClass(WorkerClassName, instance);
}

static DWORD WorkerThread(LPVOID data)
{
    struct WorkerArg* arg = (struct WorkerArg*)data;
    // PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);
    arg->workerWin = CreateWindowEx(WS_EX_TOPMOST, WorkerClassName, NULL, WS_POPUP,
        0, 0, 0, 0, HWND_MESSAGE, NULL, arg->instance, arg);
    SetEvent(arg->workerReady);

    MSG msg = {};
    while (GetMessage(&msg, arg->workerWin, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-postthreadmessagea
void ApplyWithTimeout(void (*fn)(void*), void* data, HINSTANCE instance)
{
    struct WorkerArg wa = {
        .fn = fn,
        .data = data,
        .workerReady = CreateEvent(NULL, TRUE, FALSE, "workerReady"),
        .instance = instance
    };
    DWORD tid;
    HANDLE ht = CreateThread(NULL, 0, WorkerThread, (void*)&wa, 0, &tid);
    WaitForSingleObject(wa.workerReady, INFINITE);
    SetForegroundWindow(wa.workerWin);
    SetFocus(wa.workerWin);
    PostMessage(wa.workerWin, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(ht, 500) != WAIT_OBJECT_0)
        TerminateThread(ht, 0);
    CloseHandle(ht);
    CloseHandle(wa.workerReady);
}
