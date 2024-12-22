#include "GUI.h"
#include <stdio.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#define WIN_PAD 10
#define LINE_PAD 4
#define DARK_COLOR 0x002C2C2C;
#define LIGHT_COLOR 0x00FFFFFF;

typedef struct EnumString
{
    const char* Name;
    unsigned int Value;
} EnumString;

void CreateText(Cell c, HWND parent, const char* text, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND label = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, // notify needed to tooltip
        c._X, c._Y, c._W, c._H,
        parent, NULL, inst, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM)guiData->_FontTitle, true);
}

void CreateTooltip(HWND parent, HWND tool, char* string)
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

void CreateLabel(Cell c, HWND parent, const char* name, const char* tooltip, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND label = CreateWindow(WC_STATIC, name,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, // notify needed to tooltip
        c._X, c._Y, c._W, c._H,
        parent, NULL, inst, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM)guiData->_Font, true);
    CreateTooltip(parent, label, (char*)tooltip);
}

void CreateFloatField(Cell c, HWND parent, const char* name, const char* tooltip, float* value, GUIData* appData)
{
    CreateLabel(c._X, c._Y, c._W / 2, c._H, parent, name, tooltip, appData);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    char sval[4] = "000";
    sprintf(sval, "%3d", (int)(*value * 100));
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        c._X + c._W / 2, c._Y, c._W / 2, c._H,
        parent, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)appData->_Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);

    appData->_FBindings[appData->_FBindingCount]._Field = field;
    appData->_FBindings[appData->_FBindingCount]._TargetValue = value;
    appData->_FBindingCount++;
}

void CreateComboBox(Cell c, HWND parent, const char* name, const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData)
{
    CreateLabel(c._X, c._Y, c._W / 2, c._H, parent, name, tooltip, appData);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
        c._X + c._W / 2, c._Y, c._W / 2, c._H,
        parent, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        SendMessage(combobox,(UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox, (UINT)CB_SETCURSEL, (WPARAM)i, (LPARAM)0);
    }
    SendMessage(combobox, WM_SETFONT, (WPARAM)guiData->_Font, true);
    CreateTooltip(parent, combobox, (char*)tooltip);

    guiData->_EBindings[guiData->_EBindingCount]._ComboBox = combobox;
    guiData->_EBindings[guiData->_EBindingCount]._EnumStrings = enumStrings;
    guiData->_EBindings[guiData->_EBindingCount]._TargetValue = value;
    guiData->_EBindingCount++;
}

void CreateButton(Cell c, HWND parent, const char* name, HMENU ID, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, name,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        c._X, c._Y, 0, 0, parent, (HMENU)ID, inst, NULL);
    SendMessage(button, WM_SETFONT, (WPARAM)guiData->_Font, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, c._X + c._W / 2 - size.cx / 2, c._Y, size.cx, c._H, 0);
}

void CreateBoolControl(Cell c, HWND parent, const char* name, const char* tooltip, bool* value, GUIData* guiData)
{
    CreateLabel(c._X, c._Y, c._W / 2, c._H, parent, name, tooltip, guiData);
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, "",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT | BS_CENTER,
        c._X + c._W / 2, c._Y, c._W / 2, c._H, parent, (HMENU)0, inst, NULL);
    SendMessage(button, BM_SETCHECK, (WPARAM)*value ? BST_CHECKED : BST_UNCHECKED, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    guiData->_BBindings[guiData->_BBindingCount]._CheckBox = button;
    guiData->_BBindings[guiData->_BBindingCount]._TargetValue = value;
    guiData->_BBindingCount++;
}

void InitGUIData(GUIData* guiData, HWND parent)
{
    NONCLIENTMETRICS metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    metrics.lfCaptionFont.lfHeight *= 1.2;
    metrics.lfCaptionFont.lfWidth *= 1.2;
    guiData->_Font = CreateFontIndirect(&metrics.lfCaptionFont);
    LOGFONT title = metrics.lfCaptionFont;
    title.lfWeight = FW_SEMIBOLD;
    guiData->_FontTitle = CreateFontIndirect(&title);
    COLORREF col = LIGHT_COLOR;
    guiData->_Background = CreateSolidBrush(col);
    guiData->_Parent = parent;
    {
        HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            parent, NULL, NULL, NULL);
        SendMessage(combobox, WM_SETFONT, (LPARAM)guiData->_Font, true);
        RECT rect = {};
        GetWindowRect(combobox, &rect);
        guiData->_CellHeight = rect.bottom -  rect.top;
        DestroyWindow(combobox);
    }
}

void DeleteGUIData(GUIData* guiData)
{
    DeleteFont(guiData->_Font);
    DeleteFont(guiData->_FontTitle);
    DeleteBrush(guiData->_Background);
    guiData->_Font = NULL;
    guiData->_FontTitle = NULL;
    guiData->_Background = NULL;
}