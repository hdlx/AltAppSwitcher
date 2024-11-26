
#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <winuser.h>
static const char CLASS_NAME[] = "AltAppSwitcherSettings";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        return 0;
    }
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    case WM_PAINT:
    {
        return 0;
    }
    case WM_ERASEBKGND:
        return (LRESULT)0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int StartSettings(HINSTANCE hInstance)
{
    {
        WNDCLASS wc = { };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.cbWndExtra = 0;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }
    HWND hwnd = CreateWindow(
        CLASS_NAME, // Window class
        "", // Window text
        WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX, // Window style
        GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2, // Pos
        300, 300, // Size
        NULL, // Parent window
        NULL, // Menu
        hInstance, // Instance handle
        NULL); // Additional application data
    (void)hwnd;

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);
    return 0;
}