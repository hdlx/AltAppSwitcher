#include "tray_window.h"
#include <winuser.h>
#include <winbase.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAYICON 1

static const char class_name[] = "aas_tray";

static LRESULT win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void tray_init(HINSTANCE instance)
{
    WNDCLASS wc = {
        .lpfnWndProc = win_proc,
        .hInstance = instance,
        .lpszClassName = class_name,
        .cbWndExtra = sizeof(void*)
        // .style = CS_HREDRAW | CS_VREDRAW,
        // .hbrBackground = GetSysColorBrush(COLOR_WINDOW)
    };

    RegisterClass(&wc);
    HWND hwnd = CreateWindowEx(
        0, // Optional window styles (WS_EX_)
        class_name, // Window class
        "", // Window text
        0, // Window style
        0, 0, 0, 0, // Pos and size
        NULL, // Parent window
        NULL, // Menu
        instance, // Instance handle
        0); // Additional application data
    (void)hwnd;

    {
        NOTIFYICONDATA nid = {
            .cbSize = sizeof(nid),
            .hWnd = hwnd,
            .uID = ID_TRAYICON,
            .uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP,
            .uCallbackMessage = WM_TRAYICON,
            .hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(101))
        };

        lstrcpy(nid.szTip, TEXT("My application"));

        Shell_NotifyIcon(NIM_ADD, &nid);

        // Recommended on modern Windows
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIcon(NIM_SETVERSION, &nid);
    }
}
