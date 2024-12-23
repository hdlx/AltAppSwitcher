#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <commctrl.h>
#include <Tlhelp32.h>
#include <stdio.h>
#include "Config/Config.h"
#include "Utils/GUI.h"

static const char CLASS_NAME[] = "AltAppSwitcherSettings";

static void RestartAAS()
{
    HANDLE procSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPPROCESS, (DWORD)0);
    PROCESSENTRY32 procEntry = {};
    procEntry.dwSize = sizeof(procEntry);
    BOOL procRes = Process32First(procSnap, &procEntry);
    while (procRes)
    {
        if (strcmp(procEntry.szExeFile, "AltAppSwitcher.exe"))
        {
            procRes = Process32Next(procSnap, &procEntry);
            continue;
        }
        {
            HANDLE threadSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPTHREAD, (DWORD)0);
            THREADENTRY32 threadEntry = {};
            threadEntry.dwSize = sizeof(threadEntry);
            BOOL threadRes = Thread32First(threadSnap, &threadEntry);
            while (threadRes)
            {
                if (procEntry.th32ProcessID == threadEntry.th32OwnerProcessID)
                    PostThreadMessage(threadEntry.th32ThreadID, MSG_RESTART_AAS, 0, 0);
                threadRes = Thread32Next(threadSnap, &threadEntry);
            }
            CloseHandle(threadSnap);
        }
        procRes = Process32Next(procSnap, &procEntry);
    }
    CloseHandle(procSnap);
    return;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static GUIData guiData = {};
    static Config config = {};

#define APPLY_BUTTON_ID 1993
    switch (uMsg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        DeleteGUIData(&guiData);
        return 0;
    }
    case WM_CREATE:
    {
        GUIData* gui = &guiData;
        Config* cfg = &config;

        LoadConfig(cfg);
        InitGUIData(gui, hwnd);

        GridLayout(1, gui);
        CreateText("Key bindings:", "", gui);

        GridLayout(4, gui);
        CreateText("App hold", "", gui);
        CreateComboBox("", &cfg->_Key._AppHold, keyES, gui);
        CreateText("App switch", "", gui);
        CreateComboBox("", &cfg->_Key._AppSwitch, keyES, gui);
        CreateText("Win hold", "", gui);
        CreateComboBox("", &cfg->_Key._WinHold, keyES, gui);
        CreateText("Win switch", "", gui);
        CreateComboBox("", &cfg->_Key._WinSwitch, keyES, gui);
        CreateText("Invert", "", gui);
        CreateComboBox("", &cfg->_Key._Invert, keyES, gui);
        CreateText("Previous app", "", gui);
        CreateComboBox("", &cfg->_Key._PrevApp, keyES, gui);

        GridLayout(1, gui);
        CreateText("Graphic options:", "", gui);

        GridLayout(2, gui);
        CreateText("Theme:", "", gui);
        CreateComboBox("Color scheme. \"Auto\" to match system's.", &cfg->_ThemeMode, themeES, gui);
        CreateText("Scale:", "", gui);
        CreateFloatField("Scale controls icon size, expressed as percentage, 100 being Windows default icon size.",
            &cfg->_Scale, gui);

        GridLayout(1, gui);
        CreateText("Other:", "", gui);

        GridLayout(2, gui);
        CreateText("Mouse:", "", gui);
        CreateBoolControl("Allow selecting entry by clicking on the UI.", &cfg->_Mouse, gui);
        CreateText("Check for updates:", "", gui);
        CreateBoolControl("", &cfg->_CheckForUpdates, gui);
        CreateText("App switcher mode:", "", gui);
        CreateComboBox("App: MacOS-like, one entry per application.\nWindow: Windows-like, one entry per window (each window is considered an independent application)",
            &cfg->_AppSwitcherMode, appSwitcherModeES, gui);

        GridLayout(1, gui);
        CreateButton("Apply", (HMENU)APPLY_BUTTON_ID, gui);

        RECT r = {};
        GetClientRect(hwnd, &r);
        r.bottom = guiData._Cell._Y;;
        AdjustWindowRect(&r, (DWORD)GetWindowLong(hwnd, GWL_STYLE), false);
        SetWindowPos(hwnd, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOMOVE);
        return 0;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam == APPLY_BUTTON_ID) && HIWORD(wParam) == BN_CLICKED)
        {
            ApplyBindings(&guiData);
            WriteConfig(&config);
            RestartAAS();
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)guiData._Background;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int StartSettings(HINSTANCE hInstance)
{
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(INITCOMMONCONTROLSEX);
    ic.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&ic);
#define LIGHT_COLOR 0x00FFFFFF;
    COLORREF col = LIGHT_COLOR;
    HBRUSH bkg = CreateSolidBrush(col);

    // Main window
    {
        // Class
        WNDCLASS wc = { };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.cbWndExtra = 0;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = bkg;
        RegisterClass(&wc);
        // Window
        const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
        DWORD winStyle = WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX;
        RECT winRect = { center[0] - 200, center[1] - 300, center[0] + 200, center[1] + 300 };
        AdjustWindowRect(&winRect, winStyle, false);
        CreateWindow(CLASS_NAME, "Alt App Switcher settings",
            winStyle,
            winRect.left, winRect.top, winRect.right - winRect.left, winRect.bottom - winRect.top,
            NULL, NULL, hInstance, NULL);
    }

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);

    DeleteBrush(bkg);

    return 0;
}