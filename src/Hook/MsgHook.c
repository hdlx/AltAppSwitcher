#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <Common/MSSCommon.h>

static HWND WinHandle;

__declspec(dllexport) void SetWinHandle(HWND win)
{
    WinHandle = win;
}

__declspec(dllexport) LRESULT Proc(int code, WPARAM wParam, LPARAM lParam)
{
    printf("\n HEY\n");
    //if (code != HC_ACTION)
    //    return CallNextHookEx(NULL, code, wParam, lParam);
    MSG* msg = (MSG*)lParam;
    if (msg->message == (WM_APP + 1))
    {
        printf("\nRECEIVING WM_USER + 1\n");
        return 0;
       // MyPrintWindow(msg->hwnd);
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}