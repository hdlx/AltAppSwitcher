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

typedef struct EnumBinding {
    unsigned int* TargetValue;
    HWND ComboBox;
    const EnumString* EnumStrings;
} EnumBinding;

typedef struct FloatBinding {
    float* TargetValue;
    HWND Field;
} FloatBinding;

typedef struct BoolBinding {
    bool* TargetValue;
    HWND CheckBox;
} BoolBinding;

typedef struct Cell {
    int X, Y, W, H;
} Cell;

typedef struct EnumString {
    const char* Name;
    unsigned int Value;
} EnumString;

typedef enum Alignment {
    AlignementLeft,
    AlignementCenter
} Alignment;

struct GUIData {
    EnumBinding EBindings[64];
    unsigned int EBindingCount;
    FloatBinding FBindings[64];
    unsigned int FBindingCount;
    BoolBinding BBindings[64];
    unsigned int BBindingCount;
    HFONT CurrentFont;
    HFONT Font;
    HFONT FontBold;
    HBRUSH Background;
    HWND Parent;
    Cell Cell;
    int Columns;
    int Column;
    Alignment Align;
    bool Close;
};

void CloseGUI(GUIData* gui)
{
    gui->Close = true;
}

static void NextCell(GUIData* guiData)
{
    guiData->Column++;
    if (guiData->Column == guiData->Columns) {
        guiData->Column = 0;
        guiData->Cell.X = WIN_PAD;
        guiData->Cell.Y += guiData->Cell.H + WIN_PAD;
    } else {
        guiData->Cell.X += guiData->Cell.W + WIN_PAD;
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
    guiData->Cell.W = (WIDTH - WIN_PAD - WIN_PAD - (WIN_PAD * (columns - 1 > 0 ? columns - 1 : 0))) / columns;
    guiData->Column = 0;
    guiData->Columns = columns;
    guiData->Cell.X = WIN_PAD;
}

HWND CreateText(const char* text, const char* tooltip, GUIData* guiData)
{
    int align = SS_LEFT;
    if (guiData->Align == AlignementCenter)
        align = SS_CENTER;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->Parent, GWLP_HINSTANCE);
    HWND textWin = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY | align, // notify needed to tooltip
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->Parent, NULL, inst, NULL);
    SendMessage(textWin, WM_SETFONT, (WPARAM)guiData->CurrentFont, true);
    CreateTooltip(guiData->Parent, textWin, (char*)tooltip);
    NextCell(guiData);
    return textWin;
}

void CreatePercentField(const char* tooltip, float* value, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->Parent, GWLP_HINSTANCE);
    char sval[] = "000";
    int a = sprintf_s(sval, sizeof(sval) / sizeof(sval[0]), "%03d", (int)(*value * 100));
    ASSERT(a > 0);
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->Parent, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)guiData->Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);
    guiData->FBindings[guiData->FBindingCount].Field = field;
    guiData->FBindings[guiData->FBindingCount].TargetValue = value;
    guiData->FBindingCount++;
    NextCell(guiData);
}

void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->Parent, GWLP_HINSTANCE);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->Parent, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++) {
        SendMessage(combobox, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox, (UINT)CB_SETCURSEL, (WPARAM)i, (LPARAM)0);
    }
    SendMessage(combobox, WM_SETFONT, (WPARAM)guiData->Font, true);
    CreateTooltip(guiData->Parent, combobox, (char*)tooltip);
    guiData->EBindings[guiData->EBindingCount].ComboBox = combobox;
    guiData->EBindings[guiData->EBindingCount].EnumStrings = enumStrings;
    guiData->EBindings[guiData->EBindingCount].TargetValue = value;
    guiData->EBindingCount++;
    NextCell(guiData);
}

HWND CreateButton(const char* text, HMENU ID, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->Parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        guiData->Cell.X, guiData->Cell.Y, 0, 0,
        guiData->Parent, (HMENU)ID, inst, NULL);
    SendMessage(button, WM_SETFONT, (WPARAM)guiData->Font, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, guiData->Cell.X + (guiData->Cell.W / 2) - (size.cx / 2), guiData->Cell.Y, size.cx, guiData->Cell.H, 0);
    NextCell(guiData);
    return button;
}

void CreateBoolControl(const char* tooltip, bool* value, GUIData* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->Parent, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, "",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT | BS_CENTER,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->Parent, (HMENU)0, inst, NULL);
    SendMessage(button, BM_SETCHECK, (WPARAM)*value ? BST_CHECKED : BST_UNCHECKED, true);
    SIZE size = {};
    Button_GetIdealSize(button, &size);
    guiData->BBindings[guiData->BBindingCount].CheckBox = button;
    guiData->BBindings[guiData->BBindingCount].TargetValue = value;
    guiData->BBindingCount++;
    NextCell(guiData);
}

static void InitGUIData(GUIData* guiData, HWND parent)
{
    NONCLIENTMETRICS metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    metrics.lfCaptionFont.lfHeight = (LONG)((float)metrics.lfCaptionFont.lfHeight * 1.2f);
    metrics.lfCaptionFont.lfWidth = (LONG)((float)metrics.lfCaptionFont.lfWidth * 1.2f);
    guiData->Font = CreateFontIndirect(&metrics.lfCaptionFont);
    LOGFONT title = metrics.lfCaptionFont;
    title.lfWeight = FW_SEMIBOLD;
    guiData->FontBold = CreateFontIndirect(&title);
    guiData->CurrentFont = guiData->Font;
    COLORREF col = LIGHT_COLOR;
    guiData->Background = CreateSolidBrush(col);
    guiData->Parent = parent;
    guiData->Align = AlignementCenter;
    {
        HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            parent, NULL, NULL, NULL);
        SendMessage(combobox, WM_SETFONT, (LPARAM)guiData->Font, true);
        RECT rect = {};
        GetWindowRect(combobox, &rect);
        guiData->Cell.H = rect.bottom - rect.top;
        DestroyWindow(combobox);
    }
    guiData->Cell.X = WIN_PAD;
    guiData->Cell.Y = WIN_PAD;
    {
        RECT parentRect = {};
        GetClientRect(guiData->Parent, &parentRect);
        guiData->Cell.W = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD);
    }
    guiData->Column = 0;
}

static void DeleteGUIData(GUIData* guiData)
{
    DeleteFont(guiData->Font);
    DeleteFont(guiData->FontBold);
    DeleteBrush(guiData->Background);
    guiData->Font = NULL;
    guiData->FontBold = NULL;
    guiData->Background = NULL;
}

static void FitParentWindow(const GUIData* gui)
{
    const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
    RECT r = {
        center[0] - (WIDTH / 2),
        center[1] - (gui->Cell.Y / 2),
        center[0] + (WIDTH / 2),
        center[1] + (gui->Cell.Y / 2)
    };
    AdjustWindowRect(&r, (DWORD)GetWindowLong(gui->Parent, GWL_STYLE), false);
    SetWindowPos(gui->Parent, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
}

void ApplyBindings(const GUIData* guiData)
{
    for (unsigned int i = 0; i < guiData->EBindingCount; i++) {
        const EnumBinding* bd = &guiData->EBindings[i];

        const unsigned int iValue = SendMessage(bd->ComboBox, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
        char sValue[64] = {};
        SendMessage(bd->ComboBox, (UINT)CB_GETLBTEXT, (WPARAM)iValue, (LPARAM)sValue);
        bool found = false;
        for (unsigned int j = 0; bd->EnumStrings[j].Value != 0xFFFFFFFF; j++) {
            if (!strcmp(bd->EnumStrings[j].Name, sValue)) {
                *bd->TargetValue = bd->EnumStrings[j].Value;
                found = true;
                break;
            }
        }
        ASSERT(found);
    }
    for (unsigned int i = 0; i < guiData->FBindingCount; i++) {
        const FloatBinding* bd = &guiData->FBindings[i];
        char text[4] = "000";
        *((DWORD*)text) = 3;
        SendMessage(bd->Field, (UINT)EM_GETLINE, (WPARAM)0, (LPARAM)text);
        *bd->TargetValue = (float)strtod(text, NULL) / 100.0f;
    }
    for (unsigned int i = 0; i < guiData->BBindingCount; i++) {
        const BoolBinding* bd = &guiData->BBindings[i];
        *bd->TargetValue = BST_CHECKED == SendMessage(bd->CheckBox, (UINT)BM_GETCHECK, (WPARAM)0, (LPARAM)0);
    }
}

typedef struct UserData {
    void (*SetupGUI)(GUIData*, void*);
    void (*ButtonMessage)(UINT, GUIData*, void*);
    void* Data;
} UserData;

static LRESULT GUIWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static GUIData guiData = {};
    static UserData* userData = 0;
    ASSERT(userData);
    if (!userData)
        return 0;
    switch (uMsg) {
    case WM_DESTROY: {
        free(userData);
        DeleteGUIData(&guiData);
        PostQuitMessage(0);
        return 0;
    }
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        userData = (UserData*)cs->lpCreateParams;
        InitGUIData(&guiData, hwnd);
        userData->SetupGUI(&guiData, userData->Data);
        FitParentWindow(&guiData);
        return 0;
    }
    case WM_COMMAND: {
        if (HIWORD(wParam) == BN_CLICKED) {
            userData->ButtonMessage(LOWORD(wParam), &guiData, userData->Data);
        }
        if (guiData.Close)
            PostQuitMessage(0);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wParam, TRANSPARENT);
        return 0; // (LRESULT)guiData.Background;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void SetBoldFont(GUIData* gui)
{
    gui->CurrentFont = gui->FontBold;
}

void SetNormalFont(GUIData* gui)
{
    gui->CurrentFont = gui->Font;
}

void AlignLeft(GUIData* gui)
{
    gui->Align = AlignementLeft;
}

void AlignCenter(GUIData* gui)
{
    gui->Align = AlignementCenter;
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
    userData->SetupGUI = setupGUI;
    userData->ButtonMessage = buttonMessage;
    userData->Data = userAppData;

    // CC
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(INITCOMMONCONTROLSEX);
    ic.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&ic);

    // Class
    COLORREF col = LIGHT_COLOR;
    HBRUSH bkg = CreateSolidBrush(col);
    WNDCLASS wc = {};
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

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteBrush(bkg);
    UnregisterClass(className, instance);
}
