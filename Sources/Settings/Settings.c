#include "Settings.h"
#include <minwindef.h>
#include <stdio.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <winnt.h>
#include <winuser.h>
#include <commctrl.h>
#include <debugapi.h>
#include <process.h>
#include <Tlhelp32.h>

#include "Config/Config.h"
#include "Error/Error.h"

static const char CLASS_NAME[] = "AltAppSwitcherSettings";

typedef struct EnumBinding
{
    unsigned int* _TargetValue;
    HWND _ComboBox;
    const EnumString* _EnumStrings;
} EnumBinding;

typedef struct FloatBinding
{
    float* _TargetValue;
    HWND _Field;
} FloatBinding;

typedef struct BoolBinding
{
    bool* _TargetValue;
    HWND _CheckBox;
} BoolBinding;

typedef struct AppData
{
    EnumBinding _EBindings[64];
    unsigned int _EBindingCount;
    FloatBinding _FBindings[64];
    unsigned int _FBindingCount;
    BoolBinding _BBindings[64];
    unsigned int _BBindingCount;
    Config _Config;
    HFONT _Font;
    HFONT _FontTitle;
    HBRUSH _Background;
} AppData;

#define WIN_PAD 10
#define LINE_PAD 4
#define DARK_COLOR 0x002C2C2C;
#define LIGHT_COLOR 0x00FFFFFF;
#define APPLY_BUTTON_ID 1993

static void CreateText(int x, int y, int width, int height, HWND parent, const char* text, AppData* appData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND label = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, // notify needed to tooltip
        x, y, width, height,
        parent, NULL, inst, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM)appData->_FontTitle, true);
}

static void CreateTooltip(HWND parent, HWND tool, char* string)
{
    HWND tt = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        0, 0, 100, 100,
        parent, NULL, (HINSTANCE)GetWindowLongPtr(parent, GWLP_HINSTANCE), NULL);

    SetWindowPos(tt, HWND_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    TOOLINFO ti = {};
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.uId = (UINT_PTR)tool;
    ti.lpszText = string;
    ti.hwnd = parent;

    SendMessage(tt, TTM_ADDTOOL, 0, (LPARAM)&ti);
    SendMessage(tt, TTM_SETMAXTIPWIDTH, 0, (LPARAM)400);
    SendMessage(tt, TTM_ACTIVATE, true, (LPARAM)NULL);
}

static void CreateLabel(int x, int y, int width, int height, HWND parent, const char* name, const char* tooltip, AppData* appData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND label = CreateWindow(WC_STATIC, name,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, // notify needed to tooltip
        x, y, width, height,
        parent, NULL, inst, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM)appData->_Font, true);
    CreateTooltip(parent, label, (char*)tooltip);
}

static void CreateFloatField(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, float* value, AppData* appData)
{
    CreateLabel(x, y, w / 2, h, parent, name, tooltip, appData);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    char sval[4] = "000";
    sprintf(sval, "%3d", (int)(*value * 100));
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        x + w / 2, y, w / 2, h,
        parent, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)appData->_Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);

    appData->_FBindings[appData->_FBindingCount]._Field = field;
    appData->_FBindings[appData->_FBindingCount]._TargetValue = value;
    appData->_FBindingCount++;
}

static void CreateComboBox(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, unsigned int* value, const EnumString* enumStrings, AppData* appData)
{
    CreateLabel(x, y, w / 2, h, parent, name, tooltip, appData);

    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
        x + w / 2, y, w / 2, h,
        parent, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        SendMessage(combobox,(UINT)CB_ADDSTRING,(WPARAM)0,(LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox,(UINT)CB_SETCURSEL,(WPARAM)i, (LPARAM)0);
    }
    SendMessage(combobox, WM_SETFONT, (WPARAM)appData->_Font, true);
    CreateTooltip(parent, combobox, (char*)tooltip);

    appData->_EBindings[appData->_EBindingCount]._ComboBox = combobox;
    appData->_EBindings[appData->_EBindingCount]._EnumStrings = enumStrings;
    appData->_EBindings[appData->_EBindingCount]._TargetValue = value;
    appData->_EBindingCount++;
}

void CreateButton(int x, int y, int w, int h, HWND parent, const char* name, HMENU ID, AppData* appData)
{
    (void)w;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, name,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        x, y, 0, 0, parent, (HMENU)ID, inst, NULL);
    SendMessage(button, WM_SETFONT, (WPARAM)appData->_Font, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, x + w / 2 - size.cx / 2, y, size.cx, h, 0);
}

void CreateBoolControl(int x, int y, int w, int h, HWND parent, const char* name, const char* tooltip, bool* value, AppData* appData)
{
    (void)w; (void)h;
    CreateLabel(x, y, w / 2, h, parent, name, tooltip, appData);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, "",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT | BS_CENTER,
        x + w / 2, y, w / 2, h, parent, (HMENU)0, inst, NULL);
    SendMessage(button, BM_SETCHECK, (WPARAM)*value ? BST_CHECKED : BST_UNCHECKED, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    appData->_BBindings[appData->_BBindingCount]._CheckBox = button;
    appData->_BBindings[appData->_BBindingCount]._TargetValue = value;
    appData->_BBindingCount++;
}

static bool KillAAS()
{
    HANDLE hSnapShot = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPALL, (DWORD)0);
    PROCESSENTRY32 pEntry;
    pEntry.dwSize = sizeof (pEntry);
    BOOL hRes = Process32First(hSnapShot, &pEntry);
    BOOL killed = false;
    while (hRes)
    {
        if (strcmp(pEntry.szExeFile, "AltAppSwitcher.exe") == 0)
        {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, 0,
                (DWORD) pEntry.th32ProcessID);
            if (hProcess != NULL)
            {
                TerminateProcess(hProcess, 9);
                CloseHandle(hProcess);
                killed |= true;
            }
        }
        hRes = Process32Next(hSnapShot, &pEntry);
    }
    CloseHandle(hSnapShot);
    return killed;
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
        DeleteFont(appData._FontTitle);
        DeleteBrush(appData._Background);
        appData._Font = NULL;
        appData._FontTitle = NULL;
        return 0;
    }
    case WM_CREATE:
    {
        LoadConfig(&appData._Config);

        {
            NONCLIENTMETRICS metrics = {};
            metrics.cbSize = sizeof(metrics);
            SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
            metrics.lfCaptionFont.lfHeight *= 1.2;
            metrics.lfCaptionFont.lfWidth *= 1.2;
            appData._Font = CreateFontIndirect(&metrics.lfCaptionFont);
            LOGFONT title = metrics.lfCaptionFont;
            title.lfWeight = FW_SEMIBOLD;
            appData._FontTitle = CreateFontIndirect(&title);
            COLORREF col = LIGHT_COLOR;
            appData._Background = CreateSolidBrush(col);
        }

        int x = WIN_PAD;
        int y = WIN_PAD;
        int h = 0;
        {
            HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
                CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0,
                hwnd, NULL, NULL, NULL);
            SendMessage(combobox, WM_SETFONT, (LPARAM)appData._Font, true);
            RECT rect = {};
            GetWindowRect(combobox, &rect);
            h = rect.bottom -  rect.top;
            DestroyWindow(combobox);
        }
        int w = 0;
        {
            RECT parentRect = {};
            GetClientRect(hwnd, &parentRect);
            w = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD);
        }

#define COMBO_BOX(NAME, TOOLTIP, VALUE, ES)\
CreateComboBox(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, ES, &appData);\
y += h + LINE_PAD;

#define TITLE(NAME)\
CreateText(x, y, w, h, hwnd, NAME, &appData);\
y += h + LINE_PAD;

#define FLOAT_FIELD(NAME, TOOLTIP, VALUE)\
CreateFloatField(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, &appData);\
y += h + LINE_PAD;

#define BOOL_FIELD(NAME, TOOLTIP, VALUE)\
CreateBoolControl(x, y, w, h, hwnd, NAME, TOOLTIP, &VALUE, &appData);\
y += h + LINE_PAD;

#define BUTTON(NAME, ID)\
CreateButton(x, y, w, h, hwnd, NAME, (HMENU)ID, &appData);\
y += h + LINE_PAD;

#define SEPARATOR()\
y += LINE_PAD * 4;

        Config* cfg = &appData._Config;
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
            for (unsigned int i = 0; i < appData._EBindingCount; i++)
            {
                const EnumBinding* bd = &appData._EBindings[i];

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
            for (unsigned int i = 0; i < appData._FBindingCount; i++)
            {
                const FloatBinding* bd = &appData._FBindings[i];
                char text[4] = "000";
                *((DWORD*)text) = 3;
                SendMessage(bd->_Field,(UINT)EM_GETLINE,(WPARAM)0, (LPARAM)text);
                *bd->_TargetValue = (float)strtod(text, NULL) / 100.0f;
            }
            for (unsigned int i = 0; i < appData._BBindingCount; i++)
            {
                const BoolBinding* bd = &appData._BBindings[i];
                *bd->_TargetValue = BST_CHECKED == SendMessage(bd->_CheckBox,(UINT)BM_GETCHECK, (WPARAM)0, (LPARAM)0);
            }
            WriteConfig(&appData._Config);
            if (KillAAS())
                system("start .\\AltAppSwitcher.exe");
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)appData._Background;
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