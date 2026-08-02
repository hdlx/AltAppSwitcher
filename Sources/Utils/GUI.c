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

typedef struct IntBinding {
    int* TargetValue;
    HWND Field;
} IntBinding;

struct key_binding {
    unsigned int* target_value;
    HWND key_input_control;
};

struct button_binding {
    void (*fn)(void*);
    void* data;
    HWND button;
};

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

struct gui_window_data_res {
    HFONT Font;
    HFONT FontBold;
    HBRUSH Background;
};

struct gui_window_data {
    struct gui_window_data_res resources;
    HWND MainWin;
    HWND ContainerWin;
    EnumBinding EBindings[64];
    unsigned int EBindingCount;
    FloatBinding FBindings[64];
    unsigned int FBindingCount;
    BoolBinding BBindings[64];
    unsigned int BBindingCount;
    IntBinding IBindings[64];
    unsigned int IBindingCount;
    struct key_binding k_bindings[64];
    unsigned int k_binding_count;
    struct button_binding but_bindings[64];
    unsigned int but_bindings_count;
    HFONT CurrentFont;
    Cell Cell;
    int Columns;
    int Column;
    Alignment Align;
    bool Close;
    void (*setup_gui)(gui_window_data*, void*);
    void* setup_gui_data;
};

static void aas_scan_to_nice_name(char* out_str, unsigned int sizeof_str, unsigned int aas_scan)
{
    UINT scan = aas_scan & 0xFF;
    UINT extended = aas_scan >> 8;
    // Built win expected scan + ext format
    LONG lp = (LONG)(scan & 0xFF) << 16;
    if (extended)
        lp |= 1 << 24;
    char key_name[128] = "";
    GetKeyNameText(lp, key_name, 128);
    for (char* c = key_name; *c != '\0'; c++)
        *c = (char)tolower((int)*c);
    (void)sprintf_s(out_str, sizeof_str, "%s (%u%s)", key_name, scan, extended ? "e" : "");
}

static void* get_create_param(LPARAM lp)
{
    return ((CREATESTRUCT*)lp)->lpCreateParams;
}

static void set_win_data(HWND win, void* d)
{
    SetWindowLongPtr(win, GWLP_USERDATA, (LONG_PTR)d);
}

static void* get_win_data(HWND win)
{
    return (void*)GetWindowLongPtr(win, GWLP_USERDATA);
}

static HINSTANCE get_instance(HWND win)
{
    return (HINSTANCE)GetWindowLongPtr(win, GWLP_HINSTANCE);
}

void CloseGUI(gui_window_data* gui)
{
    gui->Close = true;
}

static void NextCell(gui_window_data* guiData)
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
        parent, NULL, get_instance(parent), NULL);
    SetWindowPos(tt, HWND_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    TOOLINFO ti = { };
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.uId = (UINT_PTR)tool;
    ti.lpszText = string;
    ti.hwnd = parent;
    SendMessage(tt, TTM_ADDTOOL, 0, (LPARAM)&ti);
    SendMessage(tt, TTM_SETMAXTIPWIDTH, 0, (LPARAM)400);
    SendMessage(tt, TTM_ACTIVATE, true, (LPARAM)NULL);
}

static int get_client_width(HWND win)
{
    RECT r = { };
    GetClientRect(win, &r);
    return r.right - r.left;
}

void GridLayout(int columns, gui_window_data* guiData)
{
    int w = get_client_width(guiData->MainWin);
    guiData->Cell.W = (w - WIN_PAD - WIN_PAD - (WIN_PAD * (columns - 1 > 0 ? columns - 1 : 0))) / columns;
    guiData->Column = 0;
    guiData->Columns = columns;
    guiData->Cell.X = WIN_PAD;
}

int font_height(HFONT font)
{
    HDC hdc = GetDC(NULL);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);
    return tm.tmHeight;
}

HWND CreateText(const char* text, const char* tooltip, gui_window_data* guiData)
{
    {
        // Background
        CreateWindow(WC_STATIC, "",
            WS_CHILD | WS_VISIBLE, // notify needed to tooltip
            guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
            guiData->ContainerWin, NULL, get_instance(guiData->MainWin), NULL);
    }

    // Text
    int align = SS_LEFT;
    if (guiData->Align == AlignementCenter)
        align = SS_CENTER;
    int font_h = font_height(guiData->CurrentFont);
    HWND textWin = CreateWindow(WC_STATIC, text,
        WS_CHILD | WS_VISIBLE | SS_CENTER | align | SS_NOTIFY, // notify needed to tooltip
        guiData->Cell.X, guiData->Cell.Y + (guiData->Cell.H - font_h) / 2, guiData->Cell.W, font_h,
        guiData->ContainerWin, NULL, get_instance(guiData->MainWin), NULL);

    SendMessage(textWin, WM_SETFONT, (WPARAM)guiData->CurrentFont, true);
    CreateTooltip(guiData->ContainerWin, textWin, (char*)tooltip);
    NextCell(guiData);
    return textWin;
}

void CreatePercentField(const char* tooltip, float* value, gui_window_data* guiData)
{
    (void)tooltip;
    HINSTANCE inst = get_instance(guiData->MainWin);
    char sval[] = "000";
    int a = sprintf_s(sval, sizeof(sval) / sizeof(sval[0]), "%03d", (int)(*value * 100));
    ASSERT(a > 0);
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->ContainerWin, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)guiData->resources.Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);
    guiData->FBindings[guiData->FBindingCount].Field = field;
    guiData->FBindings[guiData->FBindingCount].TargetValue = value;
    guiData->FBindingCount++;
    NextCell(guiData);
}

void CreateIntField(const char* tooltip, int* value, gui_window_data* guiData)
{
    (void)tooltip;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->MainWin, GWLP_HINSTANCE);
    char sval[] = "\0\0\0";
    int a = sprintf_s(sval, sizeof(sval) / sizeof(sval[0]), "%i", (int)(*value));
    ASSERT(a > 0);
    HWND field = CreateWindow(WC_EDIT, sval,
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->ContainerWin, NULL, inst, NULL);
    SendMessage(field, WM_SETFONT, (WPARAM)guiData->resources.Font, true);
    SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);
    guiData->IBindings[guiData->IBindingCount].Field = field;
    guiData->IBindings[guiData->IBindingCount].TargetValue = value;
    guiData->IBindingCount++;
    NextCell(guiData);
}

void CreateComboBox(const char* tooltip, unsigned int* value, const EnumString* enumStrings, gui_window_data* guiData)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->MainWin, GWLP_HINSTANCE);
    HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
        CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->ContainerWin, NULL, inst, NULL);
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++) {
        SendMessage(combobox, (UINT)CB_ADDSTRING, (WPARAM)0, (LPARAM)enumStrings[i].Name);
        if (*value == enumStrings[i].Value)
            SendMessage(combobox, (UINT)CB_SETCURSEL, (WPARAM)i, (LPARAM)0);
    }
    SendMessage(combobox, WM_SETFONT, (WPARAM)guiData->resources.Font, true);
    CreateTooltip(guiData->ContainerWin, combobox, (char*)tooltip);
    guiData->EBindings[guiData->EBindingCount].ComboBox = combobox;
    guiData->EBindings[guiData->EBindingCount].EnumStrings = enumStrings;
    guiData->EBindings[guiData->EBindingCount].TargetValue = value;
    guiData->EBindingCount++;
    NextCell(guiData);
}

HWND CreateButton(const char* text, gui_window_data* guiData, void (*fn)(void*), void* data)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->MainWin, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        guiData->Cell.X, guiData->Cell.Y, 0, 0,
        guiData->ContainerWin, (HMENU)(long long)guiData->but_bindings_count, inst, NULL);
    SendMessage(button, WM_SETFONT, (WPARAM)guiData->resources.Font, true);
    SIZE size = { };
    Button_GetIdealSize(button, &size);
    SetWindowPos(button, NULL, guiData->Cell.X + (guiData->Cell.W / 2) - (size.cx / 2), guiData->Cell.Y, size.cx, guiData->Cell.H, 0);
    NextCell(guiData);

    guiData->but_bindings[guiData->but_bindings_count].fn = fn;
    guiData->but_bindings[guiData->but_bindings_count].data = data;
    guiData->but_bindings_count++;

    return button;
}

void CreateBoolControl(const char* tooltip, bool* value, gui_window_data* guiData)
{
    (void)tooltip;
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->MainWin, GWLP_HINSTANCE);
    HWND button = CreateWindow(WC_BUTTON, "",
        WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX | BS_FLAT | BS_CENTER,
        guiData->Cell.X, guiData->Cell.Y, guiData->Cell.W, guiData->Cell.H,
        guiData->ContainerWin, (HMENU)0, inst, NULL);
    SendMessage(button, BM_SETCHECK, (WPARAM)*value ? BST_CHECKED : BST_UNCHECKED, true);
    SIZE size = { };
    Button_GetIdealSize(button, &size);
    guiData->BBindings[guiData->BBindingCount].CheckBox = button;
    guiData->BBindings[guiData->BBindingCount].TargetValue = value;
    guiData->BBindingCount++;
    NextCell(guiData);
}

static void gui_window_create_resources(struct gui_window_data_res* res)
{
    COLORREF col = LIGHT_COLOR;
    res->Background = CreateSolidBrush(col);
    NONCLIENTMETRICS metrics = { };
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    metrics.lfMessageFont.lfHeight = (LONG)((float)metrics.lfCaptionFont.lfHeight * 1.2f);
    metrics.lfMessageFont.lfWidth = 0;
    res->Font = CreateFontIndirect(&metrics.lfMessageFont);
    LOGFONT title = metrics.lfMessageFont;
    title.lfWeight = FW_BOLD;
    res->FontBold = CreateFontIndirect(&title);
}

static void gui_window_destroy_resources(struct gui_window_data_res* res)
{
    DeleteFont(res->Font);
    DeleteFont(res->FontBold);
    DeleteBrush(res->Background);
    res->Font = NULL;
    res->FontBold = NULL;
    res->Background = NULL;
}

static void gui_window_begin_create_gui(gui_window_data* guiData, HWND parent)
{
    struct gui_window_data_res res = guiData->resources;
    void (*setup_gui)(gui_window_data*, void*) = guiData->setup_gui;
    void* setup_gui_data = guiData->setup_gui_data;

    *guiData = (struct gui_window_data) { };

    // restore backup... dirty.
    guiData->resources = res;
    guiData->setup_gui = setup_gui;
    guiData->setup_gui_data = setup_gui_data;

    guiData->MainWin = parent;
    guiData->ContainerWin = GetWindow(parent, GW_CHILD);
    guiData->CurrentFont = guiData->resources.Font;
    guiData->Align = AlignementCenter;
    {
        HWND combobox = CreateWindow(WC_COMBOBOX, "Combobox",
            CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0,
            parent, NULL, NULL, NULL);
        SendMessage(combobox, WM_SETFONT, (LPARAM)guiData->resources.Font, true);
        RECT rect = { };
        GetWindowRect(combobox, &rect);
        guiData->Cell.H = rect.bottom - rect.top;
        DestroyWindow(combobox);
    }
    guiData->Cell.X = WIN_PAD;
    guiData->Cell.Y = WIN_PAD;
    {
        RECT parentRect = { };
        GetClientRect(guiData->MainWin, &parentRect);
        guiData->Cell.W = (parentRect.right - parentRect.left - WIN_PAD - WIN_PAD);
    }
    guiData->Column = 0;
}

static void fit_window(const gui_window_data* gui_data)
{
    const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
    const RECT client_rect = {
        center[0] - (get_client_width(gui_data->MainWin) / 2),
        center[1] - (gui_data->Cell.Y / 2),
        center[0] + (get_client_width(gui_data->MainWin) - get_client_width(gui_data->MainWin) / 2),
        center[1] + (gui_data->Cell.Y - (gui_data->Cell.Y / 2))
    };
    {
        RECT r = client_rect;
        AdjustWindowRect(&r, (DWORD)GetWindowLong(gui_data->MainWin, GWL_STYLE), false);
        SetWindowPos(gui_data->MainWin, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
    }
}

static void fit_container(const gui_window_data* gui_data)
{
    SetWindowPos(gui_data->ContainerWin, NULL, 0, 0, get_client_width(gui_data->MainWin), gui_data->Cell.Y, SWP_NOMOVE);
}

static const char key_input_class_name[] = "key_input_ctrl";
static const char wait_input_class_name[] = "wait_input_popup";
static const char container_class_name[] = "container";

void ApplyBindings(const gui_window_data* guiData)
{
    for (unsigned int i = 0; i < guiData->EBindingCount; i++) {
        const EnumBinding* bd = &guiData->EBindings[i];

        const unsigned int iValue = SendMessage(bd->ComboBox, (UINT)CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
        char sValue[64] = { };
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
    for (unsigned int i = 0; i < guiData->IBindingCount; i++) {
        const IntBinding* bd = &guiData->IBindings[i];
        char text[] = "\0\0\0";
        *((DWORD*)text) = 3;
        SendMessage(bd->Field, (UINT)EM_GETLINE, (WPARAM)0, (LPARAM)text);
        *bd->TargetValue = (int)strtol(text, NULL, 10);
    }
    for (unsigned int i = 0; i < guiData->k_binding_count; i++) {
        const struct key_binding* bd = &guiData->k_bindings[i];
        char text[] = "\0\0\0";
        *((DWORD*)text) = 3;
        HWND field = get_win_data(bd->key_input_control);
        ASSERT(field);
        unsigned int x = (UINT64)get_win_data(field);
        *bd->target_value = x;
    }
}

static LRESULT container_win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        HWND child = (HWND)lp;
        WPARAM wParam = wp;
        HWND parent = GetParent(hwnd);
        SendMessage(parent, WM_COMMAND, wParam, (LPARAM)child);
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wp, TRANSPARENT);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static LRESULT gui_win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY: {
        gui_window_destroy_resources(&((struct gui_window_data*)get_win_data(hwnd))->resources);
        PostQuitMessage(0);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        if (!win_data)
            return 0;
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        int h = win_data->Cell.Y;
        RECT r = { 0, 0, 0, h };
        AdjustWindowRect(&r, (DWORD)GetWindowLong(win_data->MainWin, GWL_STYLE), false);
        mmi->ptMaxTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_CREATE: {
        struct gui_window_data* win_data = (struct gui_window_data*)((CREATESTRUCT*)lp)->lpCreateParams;
        set_win_data(hwnd, win_data);
        {
            CreateWindowEx(
                0,
                container_class_name,
                NULL,
                WS_CHILD | WS_VISIBLE,
                0, 0, 0, 0,
                hwnd,
                NULL,
                (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
                NULL);
        }
        gui_window_create_resources(&win_data->resources);
        gui_window_begin_create_gui(win_data, hwnd);
        win_data->setup_gui(win_data, win_data->setup_gui_data);
        fit_window(win_data);
        fit_container(win_data);
        return 0;
    }
    case WM_EXITSIZEMOVE: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
        HWND child = GetWindow(win_data->ContainerWin, GW_CHILD);
        while (child) {
            HWND next = GetWindow(child, GW_HWNDNEXT);
            DestroyWindow(child);
            child = next;
        }
        gui_window_begin_create_gui(win_data, hwnd);
        win_data->setup_gui(win_data, win_data->setup_gui_data);
        fit_container(win_data);
        SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
        RECT r = { };
        GetClientRect(win_data->MainWin, &r);
        InvalidateRect(win_data->MainWin, &r, true);
    }
    case WM_SIZE: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        RECT r = { };
        GetClientRect(win_data->MainWin, &r);
        const int content_height = win_data->Cell.Y;
        const int win_height = r.bottom - r.top;
        if (content_height < win_height) {
            // ShowScrollBar(hwnd, SB_VERT, FALSE);
            return 0;
        }
        // ShowScrollBar(hwnd, SB_VERT, TRUE);
        SCROLLINFO si = { };
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = content_height - win_height;
        // si.nPage = content_height;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        ApplyBindings(win_data);
        ASSERT(win_data);
        if (!win_data)
            return 0;
        UINT button_id = LOWORD(wp);
        if (HIWORD(wp) == BN_CLICKED)
            win_data->but_bindings[button_id].fn(win_data->but_bindings[button_id].data);
        if (win_data->Close)
            PostQuitMessage(0);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        SetBkMode((HDC)wp, TRANSPARENT);
        return 0; // (LRESULT)guiData.Background;
    }
    case WM_MOUSEWHEEL: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        int max = 0;
        {
            RECT r = { };
            GetClientRect(win_data->MainWin, &r);
            const int content_height = win_data->Cell.Y;
            const int win_height = r.bottom - r.top;
            if (content_height < win_height) {
                return 0;
            }
            max = content_height - win_height;
        }
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int lines = 30;
        int scroll = -(delta / WHEEL_DELTA) * lines;
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        GetScrollInfo(hwnd, SB_VERT, &si);
        si.fMask = SIF_POS;
        si.nPos += scroll;
        si.nPos = max(0, min(max, si.nPos));
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        {
            RECT r = { };
            GetWindowRect(win_data->ContainerWin, &r);
            SetWindowPos(win_data->ContainerWin, NULL, 0, -si.nPos, 0, 0, SWP_NOSIZE);
        }
        return 0;
    }
    case WM_VSCROLL: {
        struct gui_window_data* win_data = get_win_data(hwnd);
        int action = LOWORD(wp);
        SCROLLINFO si = { 0 };
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (action) {
        case SB_LINEUP:
            pos -= 10;
            break;
        case SB_LINEDOWN:
            pos += 10;
            break;
        case SB_PAGEUP:
            pos -= si.nPage;
            break;
        case SB_PAGEDOWN:
            pos += si.nPage;
            break;
        case SB_THUMBTRACK:
            pos = HIWORD(wp);
            break;
        default:
            break;
        }

        si.fMask = SIF_POS;
        si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        {
            RECT r = { };
            GetWindowRect(win_data->ContainerWin, &r);
            SetWindowPos(win_data->ContainerWin, NULL, 0, -pos, 0, 0, SWP_NOSIZE);
        }
        // redraw your content with this offset
        // InvalidateRect(hwnd, NULL, TRUE);
        break;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static LRESULT popup_wait_input_win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    static char txt[] = "Waiting for keyboard input...";
    switch (msg) {
    case WM_CREATE: {
        SetWindowLongPtr(hwnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCT*)lp)->lpCreateParams);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r = { };
        GetClientRect(hwnd, &r);
        // Local space rect:
        RECT rl = {
            .left = 0,
            .top = 0,
            .right = r.right - r.left,
            .bottom = r.bottom - r.top
        };
        DrawText(hdc, txt, ARRAYSIZE(txt), &rl,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        HWND field = get_win_data(hwnd);
        ASSERT(field);
        UINT scan = (lp >> 16) & 0xFF;
        UINT extended = (lp >> 24) & 1;
        UINT aas_scan = scan | (extended << 8);
        set_win_data(field, (void*)(UINT64)aas_scan);
        char display_name[128] = "";
        aas_scan_to_nice_name(display_name, sizeof(display_name), aas_scan);
        SetWindowText(field, display_name);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static LRESULT key_input_win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT r = { };
        GetWindowRect(hwnd, &r);
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        HINSTANCE instance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        struct gui_window_data* gui = get_create_param(lp);
        ASSERT(gui);
        HWND field = NULL;
        {
            field = CreateWindow(WC_EDIT, "unset",
                WS_CHILD | WS_VISIBLE | ES_LEFT | ES_CENTER | ES_NUMBER | WS_BORDER | ES_READONLY,
                0, 0, w - h, h,
                hwnd, NULL, instance, NULL);
            SendMessage(field, WM_SETFONT, (WPARAM)gui->resources.Font, true);
            // SendMessage(field, EM_LIMITTEXT, (WPARAM)3, true);
        }
        set_win_data(hwnd, field);
        {
            (void)CreateWindowW(WC_BUTTONW, L"\u21BB",
                WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
                w - h, 0, h, h,
                hwnd, (HMENU)0, instance, NULL);
            // SendMessage(button, WM_SETFONT, (WPARAM)guiData->resources.Font, true);
            // SIZE size = { };
            // Button_GetIdealSize(button, &size);
            // SetWindowPos(button, NULL, C1.X, C1.Y, C1.W, C1.H, 0);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (HIWORD(wp) == BN_CLICKED) {
            HWND field = get_win_data(hwnd);
            ASSERT(field);
            const int center[2] = { GetSystemMetrics(SM_CXSCREEN) / 2, GetSystemMetrics(SM_CYSCREEN) / 2 };
            int w = 250;
            int h = 100;
            HWND x = CreateWindow(
                wait_input_class_name,
                "AAS settings",
                WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                center[0] - w / 2, center[1] - h / 2, w, h,
                hwnd, 0, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), field);
            // SendMessage(field, WM_SETFONT, (WPARAM)gui->Font, true);
            SetFocus(x);
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void SetBoldFont(gui_window_data* gui)
{
    gui->CurrentFont = gui->resources.FontBold;
}

void SetNormalFont(gui_window_data* gui)
{
    gui->CurrentFont = gui->resources.Font;
}

void AlignLeft(gui_window_data* gui)
{
    gui->Align = AlignementLeft;
}

void AlignCenter(gui_window_data* gui)
{
    gui->Align = AlignementCenter;
}

void WhiteSpace(gui_window_data* gui)
{
    NextCell(gui);
}

void GUIWindow(void (*setupGUI)(gui_window_data*, void*),
    void* userAppData,
    HANDLE instance, const char* className)
{
    gui_window_data win_data = {
        .setup_gui = setupGUI,
        .setup_gui_data = userAppData
    };

    // CC
    INITCOMMONCONTROLSEX ic;
    ic.dwSize = sizeof(INITCOMMONCONTROLSEX);
    ic.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&ic);

    COLORREF col = LIGHT_COLOR;
    HBRUSH bkg = CreateSolidBrush(col);

    // Class
    {
        WNDCLASS wc = {
            .lpfnWndProc = key_input_win_proc,
            .hInstance = instance,
            .lpszClassName = key_input_class_name,
            .style = CS_HREDRAW | CS_VREDRAW,
            .hbrBackground = bkg
        };
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {
            .lpfnWndProc = gui_win_proc,
            .hInstance = instance,
            .lpszClassName = className,
            .style = CS_HREDRAW | CS_VREDRAW,
            .hbrBackground = bkg
        };
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {
            .lpfnWndProc = popup_wait_input_win_proc,
            .hInstance = instance,
            .lpszClassName = wait_input_class_name,
            .style = CS_HREDRAW | CS_VREDRAW,
            .hbrBackground = bkg
        };
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {
            .hInstance = instance,
            .lpszClassName = container_class_name,
            .lpfnWndProc = container_win_proc,
            .style = 0,
            .hbrBackground = NULL
        };
        RegisterClass(&wc);
    }

    // Window
    DWORD win_style = WS_CAPTION | WS_SYSMENU | WS_BORDER | WS_VISIBLE | WS_MINIMIZEBOX | WS_VSCROLL | WS_THICKFRAME;
    CreateWindow(
        className, className,
        win_style,
        0, 0, 400, 600,
        NULL, NULL, instance, (LPVOID)&win_data);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteBrush(bkg);
    UnregisterClass(className, instance);
    UnregisterClass(container_class_name, instance);
    UnregisterClass(key_input_class_name, instance);
    UnregisterClass(wait_input_class_name, instance);
}

HWND CreateKeyInputField(gui_window_data* guiData, unsigned int* target)
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrA(guiData->MainWin, GWLP_HINSTANCE);
    Cell C = guiData->Cell;
    HWND win = CreateWindow(key_input_class_name, "",
        WS_CHILD | WS_VISIBLE,
        C.X, C.Y, C.W, C.H,
        guiData->ContainerWin, NULL, inst, guiData);

    {
        // Init text
        HWND field = get_win_data(win);
        set_win_data(field, (void*)(UINT64)*target);
        char display_name[128] = "";
        aas_scan_to_nice_name(display_name, sizeof(display_name), *target);
        SetWindowText(field, display_name);
    }
    NextCell(guiData);
    guiData->k_bindings[guiData->k_binding_count].target_value = target;
    guiData->k_bindings[guiData->k_binding_count].key_input_control = win;
    guiData->k_binding_count++;
    return win;
}