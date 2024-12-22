#include "Settings.h"
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <commctrl.h>
#include <Tlhelp32.h>
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/GUI.h"

static const char CLASS_NAME[] = "AltAppSwitcherSettings";

static bool RestartAAS()
{
    HANDLE hSnapShot = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPALL, (DWORD)0);
    PROCESSENTRY32 pEntry;
    pEntry.dwSize = sizeof (pEntry);
    BOOL hRes = Process32First(hSnapShot, &pEntry);
    BOOL killed = false;
    while (hRes)
    {
        if (strcmp(pEntry.szExeFile, "AltAppSwitcher.exe") != 0)
            hRes = Process32Next(hSnapShot, &pEntry);
        THREADENTRY32 tEntry;
        tEntry.dwSize = sizeof(tEntry);
        BOOL tRes = Thread32First(hSnapShot, &tEntry);
        while (tRes)
        {
            if (tEntry.th32OwnerProcessID == pEntry.th32ProcessID)
            {
                PostThreadMessage(tEntry.th32ThreadID, MSG_RESTART_AAS, 0, 0);
            }
            tRes = Thread32Next(hSnapShot, &tEntry);
        }
    }
    CloseHandle(hSnapShot);
    return killed;
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
        LoadConfig(&config);
        InitGUIData(&guiData, hwnd);

        int x = WIN_PAD;
        int y = WIN_PAD;
        int h = guiData._CellHeight;
        int w = 0;
        {
            RECT parentRect = {};
            GetClientRect(hwnd, &parentRect);
            w = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD);
        }
        Cell c = 

#define COMBO_BOX(NAME, TOOLTIP, VALUE, ES)\
CreateComboBox(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, ES, &guiData);\
y += h + LINE_PAD;

#define TITLE(NAME)\
CreateText(x, y, w, h, hwnd, NAME, &guiData);\
y += h + LINE_PAD;

#define FLOAT_FIELD(NAME, TOOLTIP, VALUE)\
CreateFloatField(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, &guiData);\
y += h + LINE_PAD;

#define BOOL_FIELD(NAME, TOOLTIP, VALUE)\
CreateBoolControl(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, &guiData);\
y += h + LINE_PAD;

#define BUTTON(NAME, ID)\
CreateButton(x, y, w, h, hwnd, NAME, (HMENU)ID, &guiData);\
y += h + LINE_PAD;

#define SEPARATOR()\
y += LINE_PAD * 4;

        Config* cfg = &config;  
        TITLE("Key bindings:")
        COMBO_BOX("App hold key:", "", cfg->_Key._AppHold, keyES)
        COMBO_BOX("Next app key:", "", cfg->_Key._AppSwitch, keyES)
        COMBO_BOX("Window hold key:", "", cfg->_Key._WinHold, keyES)
        COMBO_BOX("Next window key:", "", cfg->_Key._WinSwitch, keyES)
        COMBO_BOX("Invert key:", "", cfg->_Key._Invert, keyES)
        COMBO_BOX("Previous app key:", "", cfg->_Key._PrevApp, keyES)
        SEPARATOR()
        TITLE("Graphic options:")
        COMBO_BOX("Theme:", "Color scheme. \"Auto\" to match system's.", cfg->_ThemeMode, themeES)
        FLOAT_FIELD("Scale (\%)",
            "Scale controls icon size, expressed as percentage, 100 being Windows default icon size.",
            cfg->_Scale)
        SEPARATOR()
        TITLE("Other:")
        BOOL_FIELD("Allow mouse:", "Allow selecting entry by clicking on the UI.", cfg->_Mouse)
        BOOL_FIELD("Check for updates:", "", cfg->_CheckForUpdates)
        COMBO_BOX("Switcher mode:",
            "App: MacOS-like, one entry per application.\nWindow: Windows-like, one entry per window (each window is considered an independent application)",
            cfg->_AppSwitcherMode, appSwitcherModeES)
        SEPARATOR()
        BUTTON("Apply", (HMENU)APPLY_BUTTON_ID);
        y += WIN_PAD;

        RECT r = {};
        GetClientRect(hwnd, &r);
        r.bottom = y;
        AdjustWindowRect(&r, (DWORD)GetWindowLong(hwnd, GWL_STYLE), false);
        SetWindowPos(hwnd, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOMOVE);
        return 0;
    }
    case WM_COMMAND:
    {
        if (LOWORD(wParam == APPLY_BUTTON_ID) && HIWORD(wParam) == BN_CLICKED)
        {
            for (unsigned int i = 0; i < guiData._EBindingCount; i++)
            {
                const EnumBinding* bd = &guiData._EBindings[i];

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
            for (unsigned int i = 0; i < guiData._FBindingCount; i++)
            {
                const FloatBinding* bd = &guiData._FBindings[i];
                char text[4] = "000";
                *((DWORD*)text) = 3;
                SendMessage(bd->_Field,(UINT)EM_GETLINE,(WPARAM)0, (LPARAM)text);
                *bd->_TargetValue = (float)strtod(text, NULL) / 100.0f;
            }
            for (unsigned int i = 0; i < guiData._BBindingCount; i++)
            {
                const BoolBinding* bd = &guiData._BBindings[i];
                *bd->_TargetValue = BST_CHECKED == SendMessage(bd->_CheckBox,(UINT)BM_GETCHECK, (WPARAM)0, (LPARAM)0);
            }
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