#include "GUI.h"
#include <stdio.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "Utils/Error.h"

#define WIN_PAD 10
#define LINE_PAD 4
#define DARK_COLOR 0x002C2C2C
#define LIGHT_COLOR 0x00FFFFFF
#define WIDTH 400

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

typedef struct Cell
{
    int _X, _Y, _W, _H;
} Cell;

typedef struct EnumString
{
    const char* Name;
    unsigned int Value;
} EnumString;

typedef enum Alignment
{
    AlignementLeft,
    AlignementCenter
} Alignment;

struct GUIData
{
    EnumBinding _EBindings[64];
    unsigned int _EBindingCount;
    FloatBinding _FBindings[64];
    unsigned int _FBindingCount;
    BoolBinding _BBindings[64];
    unsigned int _BBindingCount;
    HFONT _CurrentFont;
    HFONT _Font;
    HFONT _FontBold;
    HBRUSH _Background;
    HWND _Parent;
    Cell _Cell;
    int _Columns;
    int _Column;
    Alignment _Align;
    bool _Close;
};

void CloseGUI(GUIData* gui)
{
    gui->_Close = true;
}

static void NextCell(GUIData* guiData)
{
    guiData->_Column++;
    if (guiData->_Column == guiData->_Columns)
    {
        guiData->_Column = 0;
        guiData->_Cell._X = WIN_PAD;
        guiData->_Cell._Y += guiData->_Cell._H + WIN_PAD;
    }
    else
    {
        guiData->_Cell._X += guiData->_Cell._W + WIN_PAD;
    }
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

void GridLayout(int columns, GUIData* guiData)
{
    guiData->_Cell._W = (WIDTH - WIN_PAD - WIN_PAD - (WIN_PAD * (columns - 1 > 0 ? columns - 1 : 0))) / columns;
    guiData->_Column = 0;
    guiData->_Columns = columns;
    guiData->_Cell._X = WIN_PAD;
}

HWND CreateText(const char* text, const char* tooltip, GUIData* guiData)
{
    int align = SS_LEFT;
    if (guiData->_Align == AlignementCenter)
        align = SS_CENTER;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    HWND textWin = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY | align,// notify needed to tooltip
        guiData->_Cell._X, guiData->_Cell._Y, guiData->_Cell._W, guiData->_Cell._H,
        guiData->_Parent, NULL, inst, NULL);
    SendMessage(textWin, WM_SETFONT, (WPARAM)guiData->_CurrentFont, true);
    CreateTooltip(guiData->_Parent, textWin, (char*)tooltip);
    NextCell(guiData);
    return textWin;
}

void CreatePercentField(const char* tooltip, float* value, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    char sval[4] = "000";
    sprintf(sval, "%03d", (int)(*value * 100));
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        guiData->_Cell._X, guiData->_Cell._Y, guiData->_Cell._W, guiData->_Cell._H,
        guiData->_Parent, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)guiData->_Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);
    guiData->_FBindings[guiData->_FBindingCount]._Field = field;
    guiData->_FBindings[guiData->_FBindingCount]._TargetValue = value;
    guiData->_FBindingCount++;
    NextCell(guiData);
}

void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
        guiData->_Cell._X, guiData->_Cell._Y, guiData->_Cell._W, guiData->_Cell._H,
        guiData->_Parent, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        SendMessage(combobox,(UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox, (UINT)CB_SETCURSEL, (WPARAM)i, (LPARAM)0);
    }
    SendMessage(combobox, WM_SETFONT, (WPARAM)guiData->_Font, true);
    CreateTooltip(guiData->_Parent, combobox, (char*)tooltip);
    guiData->_EBindings[guiData->_EBindingCount]._ComboBox = combobox;
    guiData->_EBindings[guiData->_EBindingCount]._EnumStrings = enumStrings;
    guiData->_EBindings[guiData->_EBindingCount]._TargetValue = value;
    guiData->_EBindingCount++;
    NextCell(guiData);
}

HWND CreateButton(const char* text, HMENU ID, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        guiData->_Cell._X, guiData->_Cell._Y, 0, 0,
        guiData->_Parent, (HMENU)ID, inst, NULL);
    SendMessage(button, WM_SETFONT, (WPARAM)guiData->_Font, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, guiData->_Cell._X + guiData->_Cell._W / 2 - size.cx / 2, guiData->_Cell._Y, size.cx, guiData->_Cell._H, 0);
    NextCell(guiData);
    return button;
}

void CreateBoolControl(const char* tooltip, bool* value, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, "",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT | BS_CENTER,
        guiData->_Cell._X, guiData->_Cell._Y, guiData->_Cell._W, guiData->_Cell._H,
        guiData->_Parent, (HMENU)0, inst, NULL);
    SendMessage(button, BM_SETCHECK, (WPARAM)*value ? BST_CHECKED : BST_UNCHECKED, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    guiData->_BBindings[guiData->_BBindingCount]._CheckBox = button;
    guiData->_BBindings[guiData->_BBindingCount]._TargetValue = value;
    guiData->_BBindingCount++;
    NextCell(guiData);
}

static void InitGUIData(GUIData* guiData, HWND parent)
{
    NONCLIENTMETRICS metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    metrics.lfCaptionFont.lfHeight *= 1.2;
    metrics.lfCaptionFont.lfWidth *= 1.2;
    guiData->_Font = CreateFontIndirect(&metrics.lfCaptionFont);
    LOGFONT title = metrics.lfCaptionFont;
    title.lfWeight = FW_SEMIBOLD;
    guiData->_FontBold = CreateFontIndirect(&title);
    guiData->_CurrentFont = guiData->_Font;
    COLORREF col = LIGHT_COLOR;
    guiData->_Background = CreateSolidBrush(col);
    guiData->_Parent = parent;
    guiData->_Align = AlignementCenter;
    {
        HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            parent, NULL, NULL, NULL);
        SendMessage(combobox, WM_SETFONT, (LPARAM)guiData->_Font, true);
        RECT rect = {};
        GetWindowRect(combobox, &rect);
        guiData->_Cell._H = rect.bottom -  rect.top;
        DestroyWindow(combobox);
    }
    guiData->_Cell._X = WIN_PAD;
    guiData->_Cell._Y = WIN_PAD;
    {
        RECT parentRect = {};
        GetClientRect(guiData->_Parent, &parentRect);
        guiData->_Cell._W = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD);
    }
    guiData->_Column = 0;
}

static void DeleteGUIData(GUIData* guiData)
{
    DeleteFont(guiData->_Font);
    DeleteFont(guiData->_FontBold);
    DeleteBrush(guiData->_Background);
    guiData->_Font = NULL;
    guiData->_FontBold = NULL;
    guiData->_Background = NULL;
}

static void FitParentWindow(const GUIData* gui)
{
    const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
    RECT r = { center[0] - WIDTH / 2, center[1] - gui->_Cell._Y / 2,
        center[0] + WIDTH / 2, center[1] + gui->_Cell._Y / 2 };
    AdjustWindowRect(&r, (DWORD)GetWindowLong(gui->_Parent, GWL_STYLE), false);
    SetWindowPos(gui->_Parent, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
}

void ApplyBindings(const GUIData* guiData)
{
    for (unsigned int i = 0; i < guiData->_EBindingCount; i++)
    {
        const EnumBinding* bd = &guiData->_EBindings[i];

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
    for (unsigned int i = 0; i < guiData->_FBindingCount; i++)
    {
        const FloatBinding* bd = &guiData->_FBindings[i];
        char text[4] = "000";
        *((DWORD*)text) = 3;
        SendMessage(bd->_Field,(UINT)EM_GETLINE,(WPARAM)0, (LPARAM)text);
        *bd->_TargetValue = (float)strtod(text, NULL) / 100.0f;
    }
    for (unsigned int i = 0; i <  guiData->_BBindingCount; i++)
    {
        const BoolBinding* bd = & guiData->_BBindings[i];
        *bd->_TargetValue = BST_CHECKED == SendMessage(bd->_CheckBox,(UINT)BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    }
}

typedef struct UserData
{
    void (*_SetupGUI)(GUIData*, void*);
    void (*_ButtonMessage)(UINT, GUIData*, void*);
    void* _Data;
} UserData;

static LRESULT GUIWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static GUIData guiData = {};
    static UserData* userData = 0;
    switch (uMsg)
    {
    case WM_DESTROY:
    {
        free(userData);
        DeleteGUIData(&guiData);
        PostQuitMessage(0);
        return 0;
    }
    case WM_CREATE:
    {
        CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
        userData = (UserData*)cs->lpCreateParams;
        InitGUIData(&guiData, hwnd);
        userData->_SetupGUI(&guiData, userData->_Data);
        FitParentWindow(&guiData);
        return 0;
    }
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            userData->_ButtonMessage(LOWORD(wParam), &guiData, userData->_Data);
        }
        if (guiData._Close)
            PostQuitMessage(0);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    {
        SetBkMode((HDC)wParam, TRANSPARENT);
        return 0; // (LRESULT)guiData._Background;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void SetBoldFont(GUIData* gui)
{
    gui->_CurrentFont = gui->_FontBold;
}

void SetNormalFont(GUIData* gui)
{
    gui->_CurrentFont = gui->_Font;
}

void AlignLeft(GUIData* gui)
{
    gui->_Align = AlignementLeft;
}

void AlignCenter(GUIData* gui)
{
    gui->_Align = AlignementCenter;
}

void WhiteSpace(GUIData* gui)
{
    NextCell(gui);
}

void GUIWindow(void (*setupGUI)(GUIData*, void*),
    void (*buttonMessage)(UINT, GUIData*, void*),
    void* userAppData,
    HANDLE instance, const char* className)
{
    UserData* userData = malloc(sizeof(UserData));
    userData->_SetupGUI = setupGUI;
    userData->_ButtonMessage = buttonMessage;
    userData->_Data = userAppData;

    // CC
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(INITCOMMONCONTROLSEX);
    ic.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&ic);

    // Class
    COLORREF col = LIGHT_COLOR;
    HBRUSH bkg = CreateSolidBrush(col);
    WNDCLASS wc = { };
    wc.lpfnWndProc = GUIWindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    wc.cbWndExtra = 0;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = bkg;
    RegisterClass(&wc);

    // Window
    DWORD winStyle = WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX;
    CreateWindow(className, className,
        winStyle,
        0, 0, 0, 0,
        NULL, NULL, instance, (LPVOID)userData);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteBrush(bkg);
    UnregisterClass(className, instance);
}
