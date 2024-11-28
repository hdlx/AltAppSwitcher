#include "Settings.h"
#include <minwindef.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <commctrl.h>
#include <debugapi.h>
#include "Config/Config.h"
#include "Error/Error.h"

static const char CLASS_NAME[] = "AltAppSwitcherSettings";

typedef struct EnumBinding
{
    unsigned int* _TargetValue;
    HWND _ComboBox;
    const EnumString* _EnumStrings;
} EnumBinding;

typedef struct AppData
{
    EnumBinding _Bindings[64];
    unsigned int _BindingCount;
    Config _Config;
    HFONT _Font;
} AppData;

static void CreateComboBox(int x, int y, HWND parent, const char* name, unsigned int* value, const EnumString* enumStrings, AppData* appData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);

    HWND label = CreateWindow(WC_STATIC, name,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        x, y, 100, 20, parent, NULL, inst, NULL);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox", 
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | SS_CENTER,
        x + 100, y, 100, 40, parent, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        SendMessage(combobox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox,(UINT)CB_SETCURSEL,(WPARAM)0, (LPARAM)0);
    }

    SendMessage(combobox, WM_SETTEXT, 0, (LPARAM)L"Some text");
    SendMessage(combobox, BCM_SETNOTE, 0, (LPARAM)L"with note");

    SendMessage(combobox, WM_SETFONT, (LPARAM)appData->_Font, true);
    SendMessage(label, WM_SETFONT, (LPARAM)appData->_Font, true);

    appData->_Bindings[appData->_BindingCount]._ComboBox = combobox;
    appData->_Bindings[appData->_BindingCount]._EnumStrings = enumStrings;
    appData->_Bindings[appData->_BindingCount]._TargetValue = value;
    appData->_BindingCount++;
}

void CreateButton(int x, int y, HWND parent, const char* name, HMENU ID, AppData* appData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, name,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        x, y, 0, 0, parent, (HMENU)ID, inst, NULL);
    SendMessage(button, WM_SETFONT, (LPARAM)appData->_Font, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, 0, 0, size.cx, size.cy, SWP_NOMOVE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static AppData appData = {};
#define APPLY_BUTTON_ID 1993
    switch (uMsg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        DeleteFont(appData._Font);
        appData._Font = NULL;
        return 0;
    }
    case WM_CREATE:
    {
        LoadConfig(&appData._Config);

        NONCLIENTMETRICS metrics = {};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
        metrics.lfCaptionFont.lfHeight *= 1.2;
        metrics.lfCaptionFont.lfWidth *= 1.2;
        appData._Font = CreateFontIndirect(&metrics.lfCaptionFont);

        int posX = 0;
        int posY = 0;
        CreateComboBox(posX, posY, hwnd, "Theme", &appData._Config._ThemeMode, themeES, &appData);
        posY += 40;
        CreateComboBox(posX, posY, hwnd, "App hold key", &appData._Config._Key._AppHold, keyES, &appData);
        posY += 40;
        CreateComboBox(posX, posY, hwnd, "Switcher mode", &appData._Config._AppSwitcherMode, appSwitcherModeES, &appData);
        posY += 40;
        CreateButton(posX, posY, hwnd, "Apply", (HMENU)APPLY_BUTTON_ID, &appData);
        return 0;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam == APPLY_BUTTON_ID) && HIWORD(wParam) == BN_CLICKED)
        {
            for (unsigned int i = 0; i < appData._BindingCount; i++)
            {
                const EnumBinding* bd = &appData._Bindings[i];

                const unsigned int iValue = SendMessage(bd->_ComboBox,(UINT)CB_GETCURSEL,(WPARAM)0, (LPARAM)0);
                char sValue[64] = {};
                SendMessage(bd->_ComboBox,(UINT)CB_GETLBTEXT,(WPARAM)iValue, (LPARAM)sValue);
                bool found = false;
                for (unsigned int j = 0; bd->_EnumStrings[j].Value != 0xFFFFFFFF; j++)
                {
                    if (!strcmp(bd->_EnumStrings[j].Name, sValue))
                    {
                        *bd->_TargetValue = bd->_EnumStrings[j].Value;
                        found = true;
                        break;
                    }
                }
                ASSERT(found);
            }
            WriteConfig(&appData._Config);
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int StartSettings(HINSTANCE hInstance)
{
    // Main window
    {
        // Class
        WNDCLASS wc = { };
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.cbWndExtra = 0;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
        // Window
        const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
        CreateWindow(CLASS_NAME, "Alt App Switcher settings",
            WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX,
            center[0] - 150, center[1] - 150, 300, 300,
            NULL, NULL, hInstance, NULL);
    }

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);

    return 0;
}