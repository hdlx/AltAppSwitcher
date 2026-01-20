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
#include <time.h>
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"
#include "AppModeWindow.h"
#include "Common.h"

#define ASYNC_APPLY

typedef struct KeyState {
    bool InvertKeyDown;
    bool HoldWinDown;
    bool HoldAppDown;
} KeyState;

struct AppData {
    struct Config Config;
    bool Elevated;
    HINSTANCE Instance;
    CRITICAL_SECTION WorkerCS;
    HWND app_mode_window;
    HWND win_mode_window;
};

static const struct KeyConfig* KeyConfig;
static DWORD MainThread;

// Main thread
#define MSG_INIT_APP (WM_USER + 1)
#define MSG_INIT_WIN (WM_USER + 2)
#define MSG_DEINIT (WM_USER + 3)
#define MSG_RESTORE_KEY (WM_USER + 4)

// Apply thread
#define MSG_APPLY_APP (WM_USER + 1)
#define MSG_APPLY_WIN (WM_USER + 2)
#define MSG_APPLY_APP_MOUSE (WM_USER + 3)

// Main window
#define MSG_FOCUS (WM_USER + 1)
#define MSG_REFRESH (WM_USER + 2)

static void RestoreKey(WORD keyCode)
{
    // if (GetAsyncKeyState(VK_RCONTROL) & 0x8000)
    // {
    //     printf("WHY\n");
    // }
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = 0;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);

    {
        // Needed ?
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = KeyConfig->Invert;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) {
        // printf("need reset key 0\n");
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetKeyState(VK_RCONTROL) & 0x8000) {
        // printf("need reset key 1\n");
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
}

static const char WORKER_CLASS_NAME[] = "WindowModeWorker";

#define MAX_WIN_GROUPS 64u

typedef struct SWinGroup {
    char ModuleFileName[MAX_PATH];
    ATOM WinClass;
    wchar_t AppName[MAX_PATH];
    wchar_t Caption[MAX_PATH];
    HWND Windows[MAX_WIN_GROUPS];
    uint32_t WindowCount;
} SWinGroup;

struct WindowData {
    HWND MainWin;
    int Selection;
    int MouseSelection;
    bool CloseHover;
    SWinGroup CurrentWinGroup;
    HANDLE WorkerWin;
    HMONITOR MouseMonitor;
    Config* Config;
    HINSTANCE Instance;
    CRITICAL_SECTION WorkerCS;
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
    if (!IsEligibleWindow(hwnd, windowData->Config, windowData->MouseMonitor, !windowData->Config->RestoreMinimizedWindows))
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

static void UIASetFocus(HWND win);

static void DestroyWinModeWin(HWND window)
{
    ASSERT(false);
}

static void InitializeSwitchWin(struct WindowData* windowData)
{
    HWND win = GetForegroundWindow();
    while (true) {
        if (!win || IsEligibleWindow(win, windowData->Config, windowData->MouseMonitor, false))
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
    if (windowData->Config->AppSwitcherMode == AppSwitcherModeApp)
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
        SetWindowPos(win, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_ASYNCWINDOWPOS); // Why this call?
    }
}

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

typedef struct ApplySwitchAppData {
    HWND Data[64];
    unsigned int Count;
    DWORD _fgWinThread;
} ApplySwitchAppData;

#ifdef ASYNC_APPLY
static DWORD WorkerThread(LPVOID data)
{
    struct WindowData* windowData = (struct WindowData*)data;

    HANDLE window = CreateWindowEx(WS_EX_TOPMOST, WORKER_CLASS_NAME, NULL, WS_POPUP,
        0, 0, 0, 0, HWND_MESSAGE, NULL, windowData->Instance, windowData);
    (void)window;

    EnterCriticalSection(&windowData->WorkerCS);
    windowData->WorkerWin = window;
    LeaveCriticalSection(&windowData->WorkerCS);
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnterCriticalSection(&windowData->WorkerCS);
    windowData->WorkerWin = NULL;
    LeaveCriticalSection(&windowData->WorkerCS);
    return 0;
}

void ApplyWithTimeout(struct WindowData* windowData, unsigned int msg)
{
    EnterCriticalSection(&windowData->WorkerCS);
    windowData->WorkerWin = NULL;
    LeaveCriticalSection(&windowData->WorkerCS);

    DWORD tid;
    HANDLE ht = CreateThread(NULL, 0, WorkerThread, (void*)windowData, 0, &tid);
    ASSERT(ht != NULL);

    while (true) {
        if (TryEnterCriticalSection(&windowData->WorkerCS)) {
            const bool initialized = windowData->WorkerWin != NULL;
            LeaveCriticalSection(&windowData->WorkerCS);
            if (initialized)
                break;
        }
        usleep(100);
    }

    HWND fgWin = GetForegroundWindow();
    DWORD fgWinThread = GetWindowThreadProcessId(fgWin, NULL);
    (void)fgWinThread;
    DWORD ret = 0;
    (void)ret;

    ret = SetForegroundWindow(windowData->WorkerWin);
    VERIFY(ret != 0);

    SendNotifyMessage(windowData->WorkerWin, msg, 0, 0);

    time_t start = time(NULL);
    while (true) {
        if (TryEnterCriticalSection(&windowData->WorkerCS)) {
            const bool done = windowData->WorkerWin == NULL;
            LeaveCriticalSection(&windowData->WorkerCS);
            if (done)
                break;
        }
        time_t now = time(NULL);
        const double dt = difftime(now, start);
        if (dt > 1.0)
            break;
    }

    CloseHandle(ht);
}
#endif

static void ApplySwitchWin(HWND win, bool restoreMinimized)
{
    if (restoreMinimized)
        RestoreWin(win);
    UIASetFocus(win);
}

enum Mode {
    ModeNone,
    ModeApp,
    ModeWin
};

LRESULT oldKbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.vkCode == KeyConfig->AppHold;
    const bool nextAppKey = kbStrut.vkCode == KeyConfig->AppSwitch;
    const bool prevAppKey = kbStrut.vkCode == KeyConfig->PrevApp;
    const bool winHoldKey = kbStrut.vkCode == KeyConfig->WinHold;
    const bool nextWinKey = kbStrut.vkCode == KeyConfig->WinSwitch;
    const bool invertKey = kbStrut.vkCode == KeyConfig->Invert;
    const bool tabKey = kbStrut.vkCode == VK_TAB;
    const bool shiftKey = kbStrut.vkCode == VK_LSHIFT;
    const bool escKey = kbStrut.vkCode == VK_ESCAPE;
    // Vim keys: h=left, j=up, k=down, l=right (and arrow keys)
    const bool vimNextKey = kbStrut.vkCode == 'L' || kbStrut.vkCode == 'J' || kbStrut.vkCode == VK_RIGHT || kbStrut.vkCode == VK_DOWN;
    const bool vimPrevKey = kbStrut.vkCode == 'H' || kbStrut.vkCode == 'K' || kbStrut.vkCode == VK_LEFT || kbStrut.vkCode == VK_UP;
    const bool isWatchedKey = appHoldKey || nextAppKey || prevAppKey || winHoldKey || nextWinKey || invertKey || tabKey || shiftKey || escKey || vimNextKey || vimPrevKey;
    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static enum Mode mode = ModeNone;

    const bool rel = kbStrut.flags & LLKHF_UP;

    // Update target app state
    bool bypassMsg = false;
    const enum Mode prevMode = mode;
    {
        const bool prevAppInput = prevAppKey && !rel;
        const bool escapeInput = escKey && !rel;
        const bool winHoldRelease = winHoldKey && rel;
        const bool appHoldRelease = appHoldKey && rel;
        const bool invertPush = invertKey && !rel;
        const bool invertRelease = invertKey && rel;
        const bool nextApp = nextAppKey && !rel;
        const bool prevApp = prevAppInput && !rel;
        const bool nextWin = nextWinKey && !rel;
        const bool cancel = escapeInput;
        const bool isWinHold = GetAsyncKeyState((SHORT)KeyConfig->WinHold) & 0x8000;
        const bool isAppHold = GetAsyncKeyState((SHORT)KeyConfig->AppHold) & 0x8000;
        // Vim/Arrow navigation
        const bool vimNext = vimNextKey && !rel;
        const bool vimPrev = vimPrevKey && !rel;

        // Denit.
        if (prevMode == ModeApp && appHoldRelease) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeWin && winHoldRelease) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->WinHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeApp && cancel) {
            mode = ModeNone;
            // PostThreadMessage(MainThread, MSG_CANCEL_APP, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        }

        if (nextApp && isAppHold) {
            mode = ModeApp;
            // PostThreadMessage(MainThread, MSG_NEXT_APP, 0, 0);
            bypassMsg = true;
        } else if (nextWin && isWinHold) {
            mode = ModeWin;
            // PostThreadMessage(MainThread, MSG_NEXT_WIN, 0, 0);
            bypassMsg = true;
        } else if (invertPush) {
            // PostThreadMessage(MainThread, MSG_INVERT_PUSH, 0, 0);
        } else if (invertRelease) {
            // PostThreadMessage(MainThread, MSG_INVERT_REL, 0, 0);
        }

        if (prevApp && isAppHold) // Not *else* if because the key can be shared
        {
            // PostThreadMessage(MainThread, MSG_PREV_APP, 0, 0);
            bypassMsg = true;
        }

        // Vim/Arrow key navigation (h/left/k/up = prev, l/right/j/down = next)
        if (vimNext && prevMode == ModeApp) {
            // PostThreadMessage(MainThread, MSG_NEXT_APP, 0, 0);
            bypassMsg = true;
        } else if (vimPrev && prevMode == ModeApp) {
            // PostThreadMessage(MainThread, MSG_PREV_APP, 0, 0);
            bypassMsg = true;
        } else if (vimNext && prevMode == ModeWin) {
            // PostThreadMessage(MainThread, MSG_NEXT_WIN, 0, 0);
            bypassMsg = true;
        } else if (vimPrev && prevMode == ModeWin) {
            // PostThreadMessage(MainThread, MSG_PREV_WIN, 0, 0);
            bypassMsg = true;
        }
    }

    if (bypassMsg)
        return 1;

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.vkCode == KeyConfig->AppHold;
    const bool nextAppKey = kbStrut.vkCode == KeyConfig->AppSwitch;
    const bool prevAppKey = kbStrut.vkCode == KeyConfig->PrevApp;
    const bool winHoldKey = kbStrut.vkCode == KeyConfig->WinHold;
    const bool nextWinKey = kbStrut.vkCode == KeyConfig->WinSwitch;
    const bool isWatchedKey = appHoldKey || nextAppKey || prevAppKey || winHoldKey || nextWinKey;
    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static enum Mode mode = ModeNone;

    const bool rel = kbStrut.flags & LLKHF_UP;

    // Update target app state
    bool bypassMsg = false;
    const enum Mode prevMode = mode;
    {
        const bool winHoldRelease = winHoldKey && rel;
        const bool appHoldRelease = appHoldKey && rel;
        const bool nextApp = nextAppKey && !rel;
        const bool nextWin = nextWinKey && !rel;
        const bool isWinHold = GetAsyncKeyState((SHORT)KeyConfig->WinHold) & 0x8000;
        const bool isAppHold = GetAsyncKeyState((SHORT)KeyConfig->AppHold) & 0x8000;

        // Denit.
        if ((prevMode == ModeApp && appHoldRelease)
            || (prevMode == ModeWin && winHoldRelease)) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            bypassMsg = true;
        }

        // Init
        if (prevMode == ModeNone && isAppHold && nextApp) {
            mode = ModeApp;
            PostThreadMessage(MainThread, MSG_INIT_APP, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeNone && isWinHold && nextWin) {
            mode = ModeWin;
            PostThreadMessage(MainThread, MSG_INIT_WIN, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->WinHold, 0);
            bypassMsg = true;
        }
    }

    if (bypassMsg)
        return 1;

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK WorkerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WindowData* windowData = NULL;
    switch (uMsg) {
    case WM_CREATE: {
        windowData = (struct WindowData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        return 0;
    }
    case MSG_APPLY_WIN: {
        ASSERT(windowData);
        if (!windowData)
            return 0;
        ApplySwitchWin(windowData->CurrentWinGroup.Windows[windowData->Selection], windowData->Config->RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    default: {
        break;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

typedef struct CloseThreadData {
    HWND Win[MAX_WIN_GROUPS];
    uint32_t Count;
    HWND MainWin;
} CloseThreadData;

static DWORD KbHookCb(LPVOID param)
{
    (void)param;
    ASSERT(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = {};

    while (GetMessage(&msg, NULL, 0, 0) > 0) { }

    return (DWORD)0;
}

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

int StartAltAppSwitcher(HINSTANCE instance)
{
    SetLastError(0);
    ASSERT(SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS));

    static struct AppData appData = {};
    {
        appData.Instance = instance;
        // Hook needs globals
        MainThread = GetCurrentThreadId();
        KeyConfig = &appData.Config.Key;
        // Init. and loads config
        LoadConfig(&appData.Config);
        // Patch only for runtime use. Do not patch if used for serialization.
#define PATCH_TILDE(key) (key) = (key) == VK_OEM_3 ? MapVirtualKey(41, MAPVK_VSC_TO_VK) : (key);
        PATCH_TILDE(appData.Config.Key.AppHold);
        PATCH_TILDE(appData.Config.Key.AppSwitch);
        PATCH_TILDE(appData.Config.Key.WinHold);
        PATCH_TILDE(appData.Config.Key.WinSwitch);
        PATCH_TILDE(appData.Config.Key.Invert);
        PATCH_TILDE(appData.Config.Key.PrevApp);
#undef PATCH_TILDE

        appData.Elevated = false;
        {
            HANDLE tok;
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
            TOKEN_ELEVATION elTok;
            DWORD cbSize = sizeof(TOKEN_ELEVATION);
            GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
            appData.Elevated = elTok.TokenIsElevated;
            CloseHandle(tok);
        }

        char updater[MAX_PATH] = {};
        UpdaterPath(updater);
        if (appData.Config.CheckForUpdates && access(updater, F_OK) == 0) {
            STARTUPINFO si = {};
            PROCESS_INFORMATION pi = {};
            CreateProcess(NULL, updater, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                &si, &pi);
        }
    }

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = WorkerWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = WORKER_CLASS_NAME;
        wc.cbWndExtra = sizeof(struct WindowData*);
        wc.style = 0;
        wc.hbrBackground = NULL;
        RegisterClass(&wc);
    }

    AppModeInit(instance, &appData.Config);

    HANDLE threadKbHook = CreateRemoteThread(GetCurrentProcess(), NULL, 0, KbHookCb, (void*)&appData, 0, NULL);
    (void)threadKbHook;

    AllowSetForegroundWindow(GetCurrentProcessId());

    HANDLE token;
    OpenProcessToken(
        GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
        &token);

    TOKEN_PRIVILEGES priv;
    priv.PrivilegeCount = 1;
    LookupPrivilegeValue(NULL, "SeDebugPrivilege", &(priv.Privileges[0].Luid));
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    ASSERT(AdjustTokenPrivileges(token, false, &priv, sizeof(priv), 0, 0));
    CloseHandle(token);

    ChangeWindowMessageFilter(MSG_RESTART_AAS, MSGFLT_ADD);
    ChangeWindowMessageFilter(MSG_CLOSE_AAS, MSGFLT_ADD);

    MSG msg = {};
    bool restartAAS = false;
    bool closeAAS = false;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        switch (msg.message) {
        case MSG_INIT_APP: {
            if (IsWindow(appData.win_mode_window)) {
                DestroyWinModeWin(appData.win_mode_window);
                appData.win_mode_window = NULL;
            }
            if (IsWindow(appData.app_mode_window)) {
                AppModeDestroyWindow(appData.app_mode_window);
                appData.app_mode_window = NULL;
            }
            appData.app_mode_window = AppModeCreateWindow();
            break;
        }
        case MSG_INIT_WIN: {
            ASSERT(false);
            InitializeSwitchWin(NULL);
#if false
            if (appData.Mode == ModeApp)
                DeinitApp(&appData);
            if (appData.Mode == ModeNone)
                InitializeSwitchWin(NULL);
            // appData.Selection += appData.Invert ? -1 : 1;
            appData.Selection = Modulo(appData.Selection, (int)appData.CurrentWinGroup.WindowCount);
#ifdef ASYNC_APPLY
            ApplyWithTimeout(&appData, MSG_APPLY_WIN);
#else
            HWND win = appData.CurrentWinGroup.Windows[appData.Selection];
            ApplySwitchWin(win, appData->Config.RestoreMinimizedWindows);
#endif
#endif
            break;
        }
        case MSG_DEINIT: {
            if (IsWindow(appData.win_mode_window)) {
                DestroyWinModeWin(appData.win_mode_window);
                appData.win_mode_window = NULL;
            }
            if (IsWindow(appData.app_mode_window)) {
                AppModeDestroyWindow(appData.app_mode_window);
                appData.app_mode_window = NULL;
            }
            break;
        }
        case MSG_RESTART_AAS: {
            restartAAS = true;
            break;
        }
        case MSG_CLOSE_AAS: {
            closeAAS = true;
            break;
        }
        case MSG_RESTORE_KEY: {
            RestoreKey(msg.wParam);
            break;
        }
        default:
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (restartAAS || closeAAS)
            break;
    }

    UnregisterClass(WORKER_CLASS_NAME, instance);

    if (restartAAS) {
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};

        char currentExe[MAX_PATH] = {};
        GetModuleFileName(NULL, currentExe, MAX_PATH);

        CreateProcess(NULL, currentExe, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
            &si, &pi);
    }
    return 0;
}
