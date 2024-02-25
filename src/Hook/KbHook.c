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

HINSTANCE hInst;

CALLBACK LRESULT KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{

    FILE* file = fopen("D:\\toto.txt", "a+");
    char processName[256];
    GetCurrentProcessName(processName, 256);
    MSG* lpmsg = (MSG*)lParam;
 //   if (lpmsg->message == WM_HOTKEY)
    {
        fprintf(file, "MSG %i\n", lpmsg->message);
    }
    fclose(file);
    return 1;

    fclose(file);
    return CallNextHookEx(NULL, nCode, wParam, lParam);
/*
    printf("Hook\n");
    return CallNextHookEx(NULL, nCode, wParam, lParam);

    KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    const bool isTab = kbStrut.vkCode == VK_TAB;
    const bool isAlt = kbStrut.vkCode == VK_LMENU;
    const bool isShift = kbStrut.vkCode == VK_LSHIFT;
    const bool releasing = kbStrut.flags & LLKHF_UP;
    const bool altDown = kbStrut.flags & LLKHF_ALTDOWN;

    uint32_t Data =
        (isTab & 0x1)       << 1 |
        (isAlt & 0x1)       << 2 |
        (isShift & 0x1)     << 3 |
        (releasing & 0x1)   << 4;

    const bool bypassMsg = isTab && altDown;

    if (isTab || isAlt || isShift)
        SendMessage(WinHandle, WM_APP, (*(WPARAM*)(&Data)), 0);

    if (bypassMsg)
        return 1;
    return CallNextHookEx(NULL, nCode, wParam, lParam);*/
}

static __thread HHOOK HookHandle;

bool APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpRes)
{
    FILE* file = fopen("D:\\toto.txt", "a+");
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        char processName[256];
        GetCurrentProcessName(processName, 256);
        Lowercase(processName);
        fprintf(file, "DLL process attach function called from %s \n", processName);
        HookHandle = SetWindowsHookEx(WH_GETMESSAGE, KeyboardProc, 0, GetCurrentThreadId());
        if (!HookHandle)
           FPrintLastError();
        break;
    }
    case DLL_THREAD_ATTACH:

        char processName[256];
        GetCurrentProcessName(processName, 256);
        Lowercase(processName);
        fprintf(file, "DLL thread attach function called from %s \n", processName);
        HookHandle = SetWindowsHookEx(WH_GETMESSAGE, KeyboardProc, 0, GetCurrentThreadId());
        if (!HookHandle)
           FPrintLastError();

        break;
    case DLL_THREAD_DETACH:
        UnhookWindowsHookEx(HookHandle);
        break;
    case DLL_PROCESS_DETACH:
        UnhookWindowsHookEx(HookHandle);
        break;
    }
    fclose(file);

    return true;
}
/*
__declspec(dllexport) LRESULT WINAPI SomeProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(NULL, nCode, wParam,lParam);

    FILE* file = fopen("D:\\toto.txt", "a+");
    char processName[256];
    GetCurrentProcessName(processName, 256);
    fprintf(file, "SomeProc function called from %s \n", processName);
    fclose(file);
}

__declspec(dllexport) HHOOK SetHook(DWORD targetTid) {
    return SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, (HINSTANCE)hInst, targetTid);
    //HHOOK h = SetWindowsHookEx(WH_CALLWNDPROC, SomeProc, hInst, targetTid);
    //VERIFY(h);
    //UnhookWindowsHookEx(h);
   // return h;
}
*/