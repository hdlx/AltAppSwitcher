
#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <winuser.h>
#include <commctrl.h>
static const char CLASS_NAME[] = "AltAppSwitcherSettings";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
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
    const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
    HWND mainWin = CreateWindow(
        CLASS_NAME, // Window class
        "", // Window text
        WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX, // Window style
        center[0] - 150, center[1] - 150, // Pos
        300, 300, // Size
        NULL, // Parent window
        NULL, // Menu
        hInstance, // Instance handle
        NULL); // Additional application data

    HWND button = CreateWindow( 
        "BUTTON",
        "OK",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        10,
        10,
        100,
        100,
        mainWin,
        NULL,
        hInstance, 
        NULL);
    SIZE size = { 0, 0 };
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, 0, 0, size.cx, size.cy, SWP_NOREPOSITION);
    (void)button;

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);
    return 0;
}