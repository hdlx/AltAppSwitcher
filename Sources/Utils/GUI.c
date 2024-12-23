#include "GUI.h"
#include <stdio.h>
#include <windef.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "Utils/Error.h"

#define WIN_PAD 10
#define LINE_PAD 4
#define DARK_COLOR 0x002C2C2C;
#define LIGHT_COLOR 0x00FFFFFF;

typedef struct EnumString
{
    const char* Name;
    unsigned int Value;
} EnumString;

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
    {
        RECT parentRect = {};
        GetClientRect(guiData->_Parent, &parentRect);
        guiData->_Cell._W = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD - (WIN_PAD * (columns - 1 > 0 ? columns - 1 : 0))) / columns;
    }
    guiData->_Column = 0;
    guiData->_Columns = columns;
    guiData->_Cell._X = WIN_PAD;

}

void CreateText(const char* text, const char* tooltip, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    HWND label = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE | SS_NOTIFY, // notify needed to tooltip
        guiData->_Cell._X, guiData->_Cell._Y, guiData->_Cell._W, guiData->_Cell._H,
        guiData->_Parent, NULL, inst, NULL);
    SendMessage(label, WM_SETFONT, (WPARAM)guiData->_Font, true);
    CreateTooltip(guiData->_Parent, label, (char*)tooltip);
    NextCell(guiData);
}

void CreateFloatField(const char* tooltip, float* value, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->_Parent, GWLP_HINSTANCE);
    char sval[4] = "000";
    sprintf(sval, "%3d", (int)(*value * 100));
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

void CreateButton(const char* text, HMENU ID, GUIData* guiData)
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

void DeleteGUIData(GUIData* guiData)
{
    DeleteFont(guiData->_Font);
    DeleteFont(guiData->_FontTitle);
    DeleteBrush(guiData->_Background);
    guiData->_Font = NULL;
    guiData->_FontTitle = NULL;
    guiData->_Background = NULL;
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