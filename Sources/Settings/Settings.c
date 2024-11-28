
#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <winuser.h>
#include <commctrl.h>
#include <debugapi.h>
#include "Config/Config.h"

static const char CLASS_NAME[] = "AltAppSwitcherSettings";

typedef struct EnumBinding
{
    unsigned int* TargetValue;
    HWND ComboBox;
    const EnumString* EnumStrings;
} EnumBinding;

typedef struct EnumBindings
{
    unsigned int Count;
    EnumBinding Data[64];
} EnumBindings;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static EnumBindings* bindings = NULL; 
    switch (uMsg)
    {
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        return 0;
    }
    case WM_CREATE:
    {
        bindings = (EnumBindings*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        return 0;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam == 666) && HIWORD(wParam) == BN_CLICKED)
        {
            HWND button = (HWND)lParam;
            (void)button;
            (void)bindings;
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void CreateComboBox(int x, int y, HWND parent, HINSTANCE instance, const char* name, unsigned int* value, const EnumString* enumStrings, EnumBindings* bindings)
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
        if (*value == enumStrings[i].Value)
            SendMessage(combobox,(UINT)CB_SETCURSEL,(WPARAM)0, (LPARAM)0);
    }


    //SendMessage(hwndCommandLink, WM_SETTEXT, 0, (LPARAM)L"Command link");
    //SendMessage(hwndCommandLink, BCM_SETNOTE, 0, (LPARAM)L"with note");

    bindings->Data[bindings->Count].ComboBox = combobox;
    bindings->Data[bindings->Count].EnumStrings = enumStrings;
    bindings->Data[bindings->Count].TargetValue = value;
    bindings->Count++;
}

int StartSettings(HINSTANCE hInstance)
{
    EnumBindings bindings;
    memset(&bindings, 0, sizeof(bindings));

    // Main window
    HWND mainWin = NULL;
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
        mainWin = CreateWindow(
            CLASS_NAME, // Window class
            "", // Window text
            WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX, // Window style
            center[0] - 150, center[1] - 150, // Pos
            300, 300, // Size
            NULL, // Parent window
            NULL, // Menu
            hInstance, // Instance handle
            &bindings); // Additional application data
    }

    Config cfg;
    LoadConfig(&cfg);

    int posX = 0;
    int posY = 0;

    CreateComboBox(posX, posY, mainWin, hInstance, "Theme", &cfg._ThemeMode, themeES, &bindings);
    posY += 40;
    CreateComboBox(posX, posY, mainWin, hInstance, "App hold key", &cfg._Key._AppHold, keyES, &bindings);
    posY += 40;
    CreateComboBox(posX, posY, mainWin, hInstance, "Switcher mode", &cfg._AppSwitcherMode, appSwitcherModeES, &bindings);
    posY += 40;

    HWND button = CreateWindow(WC_BUTTON, "Apply",
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        posX, posY, 100, 20, mainWin, (HMENU)666, hInstance, NULL);
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