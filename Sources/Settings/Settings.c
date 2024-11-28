
#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <winuser.h>
#include <commctrl.h>
#include <debugapi.h>
#include "Config/Config.h"

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

typedef struct EnumBinding
{
    unsigned int* TargetValue;
    HWND ComboBox;
    EnumString* EnumStrings;
} EnumBinding;

static HWND CreateComboBox(int x, int y, HWND parent, HINSTANCE instance, const char* name, unsigned int value, const EnumString* enumStrings)
{
    CreateWindow(WC_STATIC, name,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
        x, y, 100, 40, parent, NULL, instance, NULL);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox", 
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE | SS_CENTER,
        x + 100, y, 100, 40, parent, NULL, instance, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        SendMessage(combobox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)enumStrings[i].Name);
        if (value == enumStrings[i].Value)
            SendMessage(combobox,(UINT)CB_SETCURSEL,(WPARAM)0,(LPARAM)0);
    }
    return combobox;
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

    Config cfg;
    LoadConfig(&cfg);

    int posX = 0;
    int posY = 0;

    CreateComboBox(posX, posY, mainWin, hInstance, "Theme", cfg._ThemeMode, themeES);
    posY += 40;
    CreateComboBox(posX, posY, mainWin, hInstance, "App hold key", cfg._Key._AppHold, keyES);
    posY += 40;
    CreateComboBox(posX, posY, mainWin, hInstance, "Switcher mode", cfg._AppSwitcherMode, appSwitcherModeES);
    posY += 40;

    HWND button = CreateWindow(WC_BUTTON, "Apply",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        posX, posY, 100, 20, mainWin, NULL, hInstance, NULL);
    (void)button;

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnregisterClass(CLASS_NAME, hInstance);
    

    WriteConfig(&cfg);
    return 0;
}