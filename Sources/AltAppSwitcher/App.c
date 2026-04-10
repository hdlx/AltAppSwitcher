#define COBJMACROS
#include <string.h>
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
#include <Shobjidl.h>
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"
#include "AppMode.h"
#include "WinMode.h"
#include "Common.h"
#include "Messages.h"

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
    HWND app_mode_window;
    HWND win_mode_window;
    IVirtualDesktopManager* VDM;
};

static struct Config* Cfg;
static DWORD MainThread;

static void RestoreKey(WORD keyCode)
{
    {
        INPUT input = { };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = 0;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);

    {
        // Needed ?
        INPUT input = { };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = Cfg->KeyScanCodes.Invert;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
    {
        INPUT input = { };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) {
        // printf("need reset key 0\n");
        INPUT input = { };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetKeyState(VK_RCONTROL) & 0x8000) {
        // printf("need reset key 1\n");
        INPUT input = { };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
}

enum Mode {
    ModeNone,
    ModeApp,
    ModeWin
};

static DWORD ThreadFnRestoreKey(LPVOID param)
{
    WORD keyCode = (WORD)(UINT_PTR)param;
    RestoreKey(keyCode);
    return 0;
}

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.scanCode == Cfg->KeyScanCodes.AppHold;
    const bool nextAppKey = kbStrut.scanCode == Cfg->KeyScanCodes.AppSwitch;
    const bool prevAppKey = kbStrut.scanCode == Cfg->KeyScanCodes.PrevApp;
    const bool winHoldKey = kbStrut.scanCode == Cfg->KeyScanCodes.WinHold;
    const bool nextWinKey = kbStrut.scanCode == Cfg->KeyScanCodes.WinSwitch;
    const bool isWatchedKey = appHoldKey || nextAppKey || prevAppKey || winHoldKey || nextWinKey; // NOLINT
    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static enum Mode mode = ModeNone;

    const bool rel = (kbStrut.flags & LLKHF_UP) != 0;

    // Update target app state
    bool bypassMsg = false;
    const enum Mode prevMode = mode;
    {
        const bool winHoldRelease = (winHoldKey && rel) != 0;
        const bool appHoldRelease = appHoldKey && rel;
        const bool nextApp = nextAppKey && !rel;
        const bool nextWin = nextWinKey && !rel;
        const bool isWinHold = GetAsyncKeyState(MapVirtualKeyEx(Cfg->KeyScanCodes.WinHold, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0))) & 0x8000;
        const bool isAppHold = GetAsyncKeyState(MapVirtualKeyEx(Cfg->KeyScanCodes.AppHold, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0))) & 0x8000;

        // Denit.
        if ((prevMode == ModeApp && appHoldRelease)
            || (prevMode == ModeWin && winHoldRelease)) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            bypassMsg = true;
        }

        // Init
        if (prevMode == ModeNone && isAppHold && nextApp) {
            HANDLE ht = CreateThread(NULL, 0, ThreadFnRestoreKey, (LPVOID)(UINT_PTR)Cfg->KeyScanCodes.AppHold, CREATE_SUSPENDED, NULL);
            SetThreadPriority(ht, THREAD_PRIORITY_TIME_CRITICAL);
            ResumeThread(ht);
            mode = ModeApp;
            PostThreadMessage(MainThread, MSG_INIT_APP, 0, 0);
            bypassMsg = true;
        } else if (prevMode == ModeNone && isWinHold && nextWin) {
            HANDLE ht = CreateThread(NULL, 0, ThreadFnRestoreKey, (LPVOID)(UINT_PTR)Cfg->KeyScanCodes.WinHold, CREATE_SUSPENDED, NULL);
            SetThreadPriority(ht, THREAD_PRIORITY_TIME_CRITICAL);
            ResumeThread(ht);
            mode = ModeWin;
            PostThreadMessage(MainThread, MSG_INIT_WIN, 0, 0);
            bypassMsg = true;
        }
    }

    if (bypassMsg)
        return 1;

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static DWORD KbHookCb(LPVOID param)
{
    (void)param;
    ASSERT(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = { };

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

static void AssertSingleInstance()
{
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\AltAppSwitcher{4fb3d3f7-9f35-41ce-b4d2-83c18eac3f54}");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        ExitProcess(1);
    }
}

static void ClearInitMsgs()
{
    MSG msg = { };
    while (PeekMessage(&msg, NULL, MSG_INIT_APP, MSG_INIT_WIN, PM_REMOVE) > 0) { };
}

// static void PatchKeyCode(unsigned int* keyCode)
// {
//     static HKL kbLayout = 0;
//     if (!kbLayout)
//         LoadKeyboardLayoutA("00000409", KLF_NOTELLSHELL); // Us layout
//     int scanCode = MapVirtualKeyEx(*keyCode, MAPVK_VK_TO_VSC_EX, kbLayout);
//     *keyCode = MapVirtualKeyEx(scanCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
// }

static unsigned int USKeyToScanCode(unsigned int keyCode)
{
    static HKL kbLayout = 0;
    if (!kbLayout)
        kbLayout = LoadKeyboardLayoutA("00000409", KLF_NOTELLSHELL); // Us layout
    int scanCode = MapVirtualKeyEx(keyCode, MAPVK_VK_TO_VSC_EX, kbLayout);
    return scanCode;
}

int StartAltAppSwitcher(HINSTANCE instance)
{
    SetLastError(0);
    AssertSingleInstance();
    ASSERT(SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS));
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    CoInitialize(NULL);

    static struct AppData appData = { };
    {
        appData.Instance = instance;
        // Hook needs globals
        MainThread = GetCurrentThreadId();
        // Init. and loads config
        LoadConfig(&appData.Config);
        // Patch only for runtime use.
        appData.Config.KeyScanCodes.AppHold = USKeyToScanCode(appData.Config.Key.AppHold);
        appData.Config.KeyScanCodes.AppSwitch = USKeyToScanCode(appData.Config.Key.AppSwitch);
        appData.Config.KeyScanCodes.WinHold = USKeyToScanCode(appData.Config.Key.WinHold);
        appData.Config.KeyScanCodes.WinSwitch = USKeyToScanCode(appData.Config.Key.WinSwitch);
        appData.Config.KeyScanCodes.Invert = USKeyToScanCode(appData.Config.Key.Invert);
        appData.Config.KeyScanCodes.PrevApp = USKeyToScanCode(appData.Config.Key.PrevApp);
        appData.Config.KeyScanCodes.AppClose = USKeyToScanCode(appData.Config.Key.AppClose);
        Cfg = &appData.Config;

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

        char updater[MAX_PATH] = { };
        UpdaterPath(updater);
        if (appData.Config.CheckForUpdates && access(updater, F_OK) == 0) {
            STARTUPINFO si = { };
            PROCESS_INFORMATION pi = { };
            CreateProcess(NULL, updater, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                &si, &pi);
        }

        CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL, &IID_IVirtualDesktopManager, (void**)&appData.VDM);
    }

    CommonInit(instance);
    AppModeInit(instance, &appData.Config, appData.VDM);
    WinModeInit(instance, &appData.Config, appData.VDM);

    HANDLE threadKbHook = CreateThread(NULL, 0, KbHookCb, (void*)&appData, CREATE_SUSPENDED, NULL);
    (void)threadKbHook;
    SetThreadPriority(threadKbHook, THREAD_PRIORITY_TIME_CRITICAL);
    ResumeThread(threadKbHook);
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

    MSG msg = { };
    bool restartAAS = false;
    bool closeAAS = false;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        switch (msg.message) {
        case MSG_INIT_APP: {
            ClearInitMsgs();
            RestoreKey(appData.Config.Key.WinHold);
            if (IsWindow(appData.win_mode_window)) {
                WinModeDestroyWindow(appData.win_mode_window);
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
            ClearInitMsgs();
            RestoreKey(appData.Config.Key.AppHold);
            if (IsWindow(appData.win_mode_window)) {
                WinModeDestroyWindow(appData.win_mode_window);
                appData.win_mode_window = NULL;
            }
            if (IsWindow(appData.app_mode_window)) {
                AppModeDestroyWindow(appData.app_mode_window);
                appData.app_mode_window = NULL;
            }
            appData.win_mode_window = WinModeCreateWindow();
            break;
        }
        case MSG_DEINIT: {
            if (IsWindow(appData.win_mode_window)) {
                WinModeDestroyWindow(appData.win_mode_window);
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
            ASSERT(false);
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

    CommonDeinit(instance);
    AppModeDeinit();
    WinModeDeinit();

    if (restartAAS) {
        STARTUPINFO si = { };
        PROCESS_INFORMATION pi = { };

        char currentExe[MAX_PATH] = { };
        GetModuleFileName(NULL, currentExe, MAX_PATH);

        CreateProcess(NULL, currentExe, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
            &si, &pi);
    }

    if (appData.VDM)
        IVirtualDesktopManager_Release(appData.VDM);
    CoUninitialize();
    return 0;
}
