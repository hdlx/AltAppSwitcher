#include "AppMode.h"
#define COBJMACROS
#include <string.h>
#include <wchar.h>
#include <windef.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <winnt.h>
#include <winscard.h>
#include <processthreadsapi.h>
#include <appmodel.h>
#include <unistd.h>
#include <uiautomationclient.h>
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"
#include "Common.h"
#include "Messages.h"

#define ASYNC_APPLY

static const char MAIN_CLASS_NAME[] = "WindowModeMain";

#define MAX_WIN_GROUPS 64u

typedef struct SWinGroup {
    char ModuleFileName[MAX_PATH];
    ATOM WinClass;
    HWND Windows[MAX_WIN_GROUPS];
    uint32_t WindowCount;
} SWinGroup;

struct StaticData {
    const struct Config* Config;
    HMODULE Instance;
};

struct StaticData StaticData = {};

struct WindowData {
    HWND MainWin;
    int Selection;
    SWinGroup CurrentWinGroup;
    HANDLE WorkerWin;
    HMONITOR MouseMonitor;
    struct StaticData* StaticData;
};

static BOOL GetProcessFileName(DWORD PID, char* outFileName)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
    GetModuleFileNameEx(process, NULL, outFileName, 512);
    CloseHandle(process);
    return true;
}

static BOOL CALLBACK FindIMEWin(HWND hwnd, LPARAM lParam)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    if (strcmp("IME", className) != 0)
        return TRUE;
    (*(HWND*)lParam) = hwnd;
    return TRUE;
}

typedef struct SFindPIDEnumFnParams {
    HWND InHostWindow;
    DWORD OutPID;
} SFindPIDEnumFnParams;

static BOOL FindPIDEnumFn(HWND hwnd, LPARAM lParam)
{
    SFindPIDEnumFnParams* pParams = (SFindPIDEnumFnParams*)lParam;
    static char className[512];
    GetClassName(hwnd, className, 512);
    if (strcmp("Windows.UI.Core.CoreWindow", className) != 0)
        return TRUE;

    DWORD PID = 0;
    DWORD TID = GetWindowThreadProcessId(hwnd, &PID);

    wchar_t UMI[512];
    BOOL isUWP = false;
    {
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        uint32_t size = 512;
        isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
    }
    if (!isUWP)
        return TRUE;

    HWND IMEWin = NULL;
    EnumThreadWindows(TID, FindIMEWin, (LPARAM)&IMEWin);
    if (IMEWin == NULL)
        return TRUE;

    HWND ownerWin = GetWindow(IMEWin, GW_OWNER);

    if (pParams->InHostWindow != ownerWin)
        return TRUE;

    pParams->OutPID = PID;

    return TRUE;
}

typedef struct SFindUWPChildParams {
    DWORD OutUWPPID;
    DWORD InHostPID;
} SFindUWPChildParams;

static BOOL FindUWPChild(HWND hwnd, LPARAM lParam)
{
    SFindUWPChildParams* pParams = (SFindUWPChildParams*)lParam;
    DWORD PID = 0;
    GetWindowThreadProcessId(hwnd, &PID);
    if (PID != pParams->InHostPID) {
        pParams->OutUWPPID = PID;
        return FALSE;
    }
    return TRUE;
}

static void FindActualPID(HWND hwnd, DWORD* PID)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    {
        wchar_t UMI[512];
        GetWindowThreadProcessId(hwnd, PID);
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *PID);
        uint32_t size = 512;
        BOOL isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
        if (isUWP) {
            return;
        }
    }

    if (!strcmp("ApplicationFrameWindow", className)) {
        {
            SFindUWPChildParams params;
            GetWindowThreadProcessId(hwnd, &(params.InHostPID));
            params.OutUWPPID = 0;
            EnumChildWindows(hwnd, FindUWPChild, (LPARAM)&params);
            if (params.OutUWPPID != 0) {
                *PID = params.OutUWPPID;
                return;
            }
        }
        {
            SFindPIDEnumFnParams params;
            params.InHostWindow = hwnd;
            params.OutPID = 0;

            EnumWindows(FindPIDEnumFn, (LPARAM)&params);

            *PID = params.OutPID;
            return;
        }
    }

    {
        GetWindowThreadProcessId(hwnd, PID);
        return;
    }
}
static BOOL IsRunWindow(HWND hwnd)
{
    {
        WINDOWINFO wi = {};
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(hwnd, &wi);
        if (wi.atomWindowType != 0x8002)
            return false;
    }

    const HWND owner = GetAncestor(hwnd, GA_ROOTOWNER);
    if (owner == NULL)
        return false;

    {
        WINDOWINFO wi = {};
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(owner, &wi);
        if (wi.atomWindowType != 0xC01A)
            return false;
    }

    return true;
}

static BOOL FillCurrentWinGroup(HWND hwnd, LPARAM lParam)
{
    struct WindowData* windowData = (struct WindowData*)(lParam);
    if (!IsEligibleWindow(hwnd, windowData->StaticData->Config, windowData->MouseMonitor, !windowData->StaticData->Config->RestoreMinimizedWindows))
        return true;
    DWORD PID = 0;
    FindActualPID(hwnd, &PID);
    SWinGroup* currentWinGroup = &windowData->CurrentWinGroup;
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run
    if (0 != strcmp(moduleFileName, currentWinGroup->ModuleFileName) || currentWinGroup->WinClass != winClass)
        return true;
    currentWinGroup->Windows[currentWinGroup->WindowCount] = hwnd;
    currentWinGroup->WindowCount++;
    return true;
}

static void InitializeSwitchWin(HWND foregroundWindow, struct WindowData* windowData)
{
    HWND win = foregroundWindow;
    ASSERT(win);
    while (true) {
        if (!win || IsEligibleWindow(win, windowData->StaticData->Config, windowData->MouseMonitor, false))
            break;
        win = GetParent(win);
    }
    if (!win)
        return;
    DWORD PID;
    FindActualPID(win, &PID);
    SWinGroup* pWinGroup = &(windowData->CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->ModuleFileName);
    pWinGroup->WinClass = IsRunWindow(win) ? 0x8002 : 0; // Run
    pWinGroup->WindowCount = 0;
    if (windowData->StaticData->Config->AppSwitcherMode == AppSwitcherModeApp)
        EnumWindows(FillCurrentWinGroup, (LPARAM)windowData);
    else {
        pWinGroup->Windows[0] = win;
        pWinGroup->WindowCount = 1;
    }
    windowData->Selection = 0;
}

static void RestoreWin(HWND win)
{
    if (!IsWindow(win))
        return;
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED) {
        ShowWindowAsync(win, SW_RESTORE);
        SetWindowPos(win, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_ASYNCWINDOWPOS | SWP_NOACTIVATE); // Why this call?
    }
}
/*
static void UIASetFocus(HWND win)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IUIAutomation* UIA = NULL;
    DWORD res = CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void**)&UIA);
    ASSERT(SUCCEEDED(res))

    IUIAutomationElement* el = NULL;
    res = IUIAutomation_ElementFromHandle(UIA, win, &el);
    VERIFY(SUCCEEDED(res));
    res = IUIAutomationElement_SetFocus(el);
    VERIFY(SUCCEEDED(res));
    IUIAutomationElement_Release(el);

    IUIAutomation_Release(UIA);
    CoUninitialize();
}
*/

typedef struct ApplySwitchAppData {
    HWND Data[64];
    unsigned int Count;
    DWORD _fgWinThread;
} ApplySwitchAppData;

static void NextWin(void* windowDataVoidPtr)
{
    struct WindowData* windowData = windowDataVoidPtr;
    ASSERT(windowData);
    if (windowData->Selection >= windowData->CurrentWinGroup.WindowCount)
        return;
    if (windowData->StaticData->Config->RestoreMinimizedWindows)
        RestoreWin(windowData->CurrentWinGroup.Windows[windowData->Selection]);
    SetWindowPos(windowData->CurrentWinGroup.Windows[windowData->Selection], windowData->MainWin, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    WINBOOL r = SetForegroundWindow(windowData->CurrentWinGroup.Windows[windowData->Selection]);
    ASSERT(r != 0);
    SetForegroundWindow(windowData->MainWin);
}

static void ApplyWin(void* windowDataVoidPtr)
{
    struct WindowData* windowData = windowDataVoidPtr;
    if (!windowData)
        return;
    // UIASetFocus(windowData->CurrentWinGroup.Windows[windowData->Selection]);
    SetForegroundWindow(windowData->MainWin);
}
#define ASYNC

#ifdef ASYNC

#endif

#if false
static BOOL GetFirstWindow(HWND win, LPARAM lParam)
{
    *(HWND*)lParam = win;
    return false;
}

static HWND GetFirstChild(HWND win)
{
    HWND child = NULL;
    EnumChildWindows(win, GetFirstWindow, (LPARAM)&child);
    return child;
}
#endif

struct MainWindowArg {
    HWND ForegroundWindow;
    struct StaticData* StaticData;
};
static LRESULT MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WindowData windowData = {};

    switch (uMsg) {
    case WM_CREATE: {
        struct MainWindowArg* arg = (struct MainWindowArg*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        windowData = (struct WindowData) {};
        windowData.MainWin = hwnd;
        windowData.StaticData = arg->StaticData;
        InitializeSwitchWin(arg->ForegroundWindow, &windowData);
        windowData.Selection = 0;
        const bool invert = GetAsyncKeyState((SHORT)windowData.StaticData->Config->Key.Invert) & 0x8000;
        windowData.Selection += invert ? -1 : 1;
        windowData.Selection = Modulo(windowData.Selection, (int)windowData.CurrentWinGroup.WindowCount);
        SetFocus(hwnd);
        SetForegroundWindow(hwnd);
#ifdef ASYNC
        ApplyWithTimeout(NextWin, &windowData, StaticData.Instance);
#else
        NextWin(&windowData);
#endif
        return 0;
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        ASSERT(windowData.StaticData);
        ASSERT(windowData.StaticData->Config);
        int x = 0;
        if (
            wParam == windowData.StaticData->Config->Key.WinSwitch
            || wParam == 'L'
            || wParam == 'J'
            || wParam == VK_RIGHT
            || wParam == VK_DOWN) {
            x = 1;
        } else if (
            wParam == windowData.StaticData->Config->Key.WinSwitch
            || wParam == 'H'
            || wParam == 'K'
            || wParam == VK_LEFT
            || wParam == VK_UP) {
            x = -1;
        }
        if (x != 0) {
            const bool invert = GetAsyncKeyState((SHORT)windowData.StaticData->Config->Key.Invert) & 0x8000;
            windowData.Selection += invert ? -x : x;
            windowData.Selection = Modulo(windowData.Selection, (int)windowData.CurrentWinGroup.WindowCount);
#ifdef ASYNC
            ApplyWithTimeout(NextWin, &windowData, StaticData.Instance);
#else
            NextWin(&windowData);
#endif
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
#ifdef ASYNC
        ApplyWithTimeout(ApplyWin, &windowData, StaticData.Instance);
#else
        ApplyWin(&windowData);
#endif
        DestroyWindow(hwnd);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void WinModeInit(HINSTANCE instance, const struct Config* cfg)
{
    StaticData.Instance = instance;
    StaticData.Config = cfg;

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = MainWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = MAIN_CLASS_NAME;
        wc.cbWndExtra = sizeof(void*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }
}

void WinModeDeinit()
{
    UnregisterClass(MAIN_CLASS_NAME, StaticData.Instance);
}

HWND WinModeCreateWindow()
{
    /*
    HWND hwnd = CreateWindowEx(
        0, // Optional window styles (WS_EX_)
        MAIN_CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP | WS_VISIBLE, // Window style
        // Pos and size
        0,
        0,
        512,
        512,
        NULL, // Parent window
        NULL, // Menu
        StaticData.Instance, // Instance handle
        &StaticData // Additional application data
    );
    */

    HWND fgwin = GetForegroundWindow();
    if (!fgwin)
        return NULL;

    struct MainWindowArg arg = {
        .StaticData = &StaticData,
        .ForegroundWindow = fgwin
    };

    HWND hwnd = CreateWindowEx(WS_EX_TOPMOST, MAIN_CLASS_NAME, NULL, WS_POPUP,
        0, 0, 0, 0, HWND_MESSAGE, NULL, StaticData.Instance, &arg);

    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    ASSERT(hwnd);
    return hwnd;
}

void WinModeDestroyWindow(HWND window)
{
    SendMessage(window, WM_CLOSE, (WPARAM)0, (LPARAM)0);
}