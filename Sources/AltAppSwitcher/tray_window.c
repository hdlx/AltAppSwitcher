#include "tray_window.h"
#include "Utils/GUI.h"
#include "Utils/Message.h"
#include "Utils/File.h"
#include "Utils/Version.h"
#include <winuser.h>
#include <winbase.h>
#include <shellapi.h>
#include <io.h>
#include <stdio.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAYICON 1

static const char class_name[] = "aas_tray";

struct aas_tray {
    HWND tray_window;
    UINT taskbar_msg;
    HMENU menu;
};

static void NotifyTaskBar(HWND win)
{
    NOTIFYICONDATA nid = {
        .cbSize = sizeof(nid),
        .hWnd = win,
        .uID = ID_TRAYICON,
        .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP,
        .uCallbackMessage = WM_TRAYICON,
        .hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101)),
    };
    char title[260];
    int a = sprintf_s(title, sizeof(title), "AltAppSwitcher - v%u.%u", AAS_MAJOR, AAS_MINOR);
    (void)a;
    lstrcpy(nid.szTip, TEXT(title));
    bool ok0 = Shell_NotifyIcon(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    bool ok1 = Shell_NotifyIcon(NIM_SETVERSION, &nid);
    (void)ok0;
    (void)ok1;
}

static LRESULT win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        set_win_data(hwnd, ((CREATESTRUCT*)lp)->lpCreateParams);
        struct aas_tray* tray = (struct aas_tray*)get_win_data(hwnd);
        tray->taskbar_msg = RegisterWindowMessage(TEXT("TaskbarCreated"));
        tray->menu = CreatePopupMenu();
        AppendMenu(tray->menu, MF_STRING, 10, TEXT("Settings"));
        AppendMenu(tray->menu, MF_STRING, 11, TEXT("Check updates"));
        AppendMenu(tray->menu, MF_STRING, 12, TEXT("Add to startup"));
        AppendMenu(tray->menu, MF_STRING, 13, TEXT("Remove from startup"));
        AppendMenu(tray->menu, MF_STRING, 14, TEXT("Close"));
        break;
    }
    case WM_TRAYICON: {
        struct aas_tray* tray = (struct aas_tray*)get_win_data(hwnd);
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU: {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(
                tray->menu,
                TPM_RIGHTBUTTON,
                pt.x,
                pt.y,
                0,
                hwnd,
                NULL);
            break;
        }
        default:
            break;
        }
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case 14:
            CloseAAS();
            break;
        case 10: {
            char settings[MAX_PATH] = { };
            SettingsPath(settings);
            if (access(settings, F_OK) == 0) {
                STARTUPINFO si = { };
                PROCESS_INFORMATION pi = { };
                CreateProcess(NULL, settings, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                    &si, &pi);
            }
            break;
        }
        case 11: {
            char updater[MAX_PATH] = { };
            UpdaterPath(updater);
            if (access(updater, F_OK) == 0) {
                STARTUPINFO si = { };
                PROCESS_INFORMATION pi = { };
                CreateProcess(updater, "--status-popup", 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                    &si, &pi);
            }
            break;
        }
        case 12: {
            char add_to_startup[MAX_PATH] = { };
            add_to_startup_path(add_to_startup);
            if (access(add_to_startup, F_OK) == 0) {
                STARTUPINFO si = { };
                PROCESS_INFORMATION pi = { };
                CreateProcess(add_to_startup, "", 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                    &si, &pi);
            }
            break;
        }
        case 13: {
            char remove_from_startup[MAX_PATH] = { };
            remove_from_startup_path(remove_from_startup);
            if (access(remove_from_startup, F_OK) == 0) {
                STARTUPINFO si = { };
                PROCESS_INFORMATION pi = { };
                CreateProcess(remove_from_startup, "", 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                    &si, &pi);
            }
            break;
        }
        default:
            break;
        }
        return 0;
    }
    default: {
        struct aas_tray* tray = (struct aas_tray*)get_win_data(hwnd);
        if (tray && msg == tray->taskbar_msg) {
            NotifyTaskBar(hwnd);
        }
        break;
    }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void tray_deinit(struct aas_tray* tray)
{
    HINSTANCE inst = get_instance(tray->tray_window);
    DestroyWindow(tray->tray_window);
    UnregisterClass(class_name, inst);
}

struct aas_tray* tray_init(HINSTANCE instance)
{
    static struct aas_tray tray = { }; // Works only if singleton.
    WNDCLASS wc = {
        .lpfnWndProc = win_proc,
        .hInstance = instance,
        .lpszClassName = class_name,
        .cbWndExtra = sizeof(void*)
        // .style = CS_HREDRAW | CS_VREDRAW,
        // .hbrBackground = GetSysColorBrush(COLOR_WINDOW)
    };

    RegisterClass(&wc);
    tray.tray_window = CreateWindowEx(
        0,
        class_name,
        "",
        0,
        0, 0, 0, 0,
        NULL,
        NULL,
        instance,
        &tray);
    NotifyTaskBar(tray.tray_window);
    return &tray;
}
