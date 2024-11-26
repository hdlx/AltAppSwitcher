
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
    SystemParametersInfo(SPI_SETFONTSMOOTHING,
                     TRUE,
                     0,
                     SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    SystemParametersInfo(SPI_SETFONTSMOOTHINGTYPE,
                        0,
                        (PVOID)FE_FONTSMOOTHINGCLEARTYPE,
                        SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); 

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

    HWND button = CreateWindow(WC_BUTTON, "Apply",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        10, 10, 100, 20, mainWin, NULL, hInstance, NULL);
    (void)button;

    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox", 
        CBS_DROPDOWN | CBS_HASSTRINGS | WS_CHILD | WS_OVERLAPPED | WS_VISIBLE,
        10, 50, 200, 200, mainWin, NULL, hInstance, NULL);
    //SendMessage(combobox,(UINT)WM_SETFONT,(WPARAM)0,(LPARAM)"Test0");
    SendMessage(combobox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)"Test0");
    SendMessage(combobox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)"Test1");
    SendMessage(combobox,(UINT)CB_SETCURSEL,(WPARAM)0,(LPARAM)0);
    (void)combobox;

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);
    return 0;
}