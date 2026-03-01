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
};

static const struct KeyConfig* KeyConfig;
static DWORD MainThread;

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

enum Mode {
    ModeNone,
    ModeApp,
    ModeWin
};

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
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            PostThreadMessage(MainThread, MSG_INIT_APP, 0, 0);
            bypassMsg = true;
        } else if (prevMode == ModeNone && isWinHold && nextWin) {
            mode = ModeWin;
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->WinHold, 0);
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
        PATCH_TILDE(appData.Config.Key.AppClose);
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

    CommonInit(instance);
    AppModeInit(instance, &appData.Config);
    WinModeInit(instance, &appData.Config);

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

    CommonDeinit(instance);
    AppModeDeinit();
    WinModeDeinit();

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
