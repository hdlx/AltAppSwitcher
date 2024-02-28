#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winuser.h>
#include "MacAppSwitcherHelpers.h"
#include <signal.h>
#include <ctype.h>
#include <processthreadsapi.h>
#include <gdiplus.h>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct SWinGroup
{
    char _ModuleFileName[512];
    HWND _Windows[64];
    uint32_t _WindowCount;
    HICON _Icon;
} SWinGroup;

typedef struct SWinGroupArr
{
    SWinGroup _Data[64];
    uint32_t _Size;
} SWinGroupArr;

typedef struct SKeyState
{
    bool _TabDown;
    bool _ShiftDown;
    bool _AltDown;
    bool _TildeDown;
    bool _TabNewInput;
    bool _TildeNewInput;
    bool _AltReleasing;
} SKeyState;

typedef struct SAppData
{
    HWND _MainWin;
    bool _WindowDisplayed;
    bool _IsSwitchingApp;
    bool _IsSwitchingWin;
    int _Selection;
    HINSTANCE _MsgHookDll;
    HOOKPROC _MsgHookProc;
    SWinGroupArr _WinGroups;
    SWinGroup _CurrentWinGroup;
    HHOOK _MsgHook;
    SKeyState _KeyState;
} SAppData;

typedef struct SFoundWin
{
    HWND _Data[64];
    uint32_t _Size;
} SFoundWin;

static HWND _MainWin;
static bool _IsSwitchActive = false;

static void DisplayWindow(HWND win)
{
    SetWindowPos(win, HWND_TOP, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
}

static void HideWindow(HWND win)
{
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
}

static const char* WindowsClassNamesToSkip[6] =
{
    "Shell_TrayWnd",
    "DV2ControlHost",
    "MsgrIMEWindowClass",
    "SysShadow",
    "Button",
    "Windows.UI.Core.CoreWindow"
};

static BOOL GetProcessFileName(DWORD PID, char* outFileName)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
    GetModuleFileNameEx(process, NULL, outFileName, 512);
    CloseHandle(process);
}

static bool IsAltTabWindow(HWND hwnd)
{
    if (hwnd == GetShellWindow()) //Desktop
        return false;
    // Start at the root owner
    const HWND hwndRoot = GetAncestor(hwnd, GA_ROOTOWNER);
    // See if we are the last active visible popup
    if (GetLastActivePopup(hwndRoot) != hwnd)
        return false;
    if (!IsWindowVisible(hwnd))
        return false;
    static char buf[512];
    GetClassName(hwnd, buf, 100);
    for (int i = 0; i < sizeof(WindowsClassNamesToSkip) / sizeof(WindowsClassNamesToSkip[0]); i++)
    {
        if (!strcmp(WindowsClassNamesToSkip[i], buf))
            return false;
    }
    WINBOOL cloaked = false;
    if (!strcmp(buf, "ApplicationFrameWindow"))
       DwmGetWindowAttribute(hwnd, (DWORD)DWMWA_CLOAKED, (PVOID)&cloaked, (DWORD)sizeof(cloaked));
    if (cloaked)
        return false;
    WINDOWINFO wi;
    GetWindowInfo(hwnd, &wi);
    if ((wi.dwExStyle & WS_EX_TOOLWINDOW) != 0)
        return false;
    return true;
}

static bool ForceSetForeground(HWND win)
{
    const DWORD dwCurrentThread = GetCurrentThreadId();
    const DWORD dwFGThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    AttachThreadInput(dwCurrentThread, dwFGThread, TRUE);
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED)
        ShowWindow(win, SW_RESTORE);
    VERIFY(BringWindowToTop(win) || SetForegroundWindow(win));
    SetFocus(win);
    SetActiveWindow(win);
    EnableWindow(win, TRUE);
    AttachThreadInput(dwCurrentThread, dwFGThread, FALSE);
    return true;
}

static BOOL FindAltTabProc(HWND hwnd, LPARAM lParam)
{
    ALTTABINFO info = {};
    if (!GetAltTabInfo(hwnd, 0, &info, NULL, 0))
        return TRUE;
    SFoundWin* pFoundWin = (SFoundWin*)(lParam);
    pFoundWin->_Data[pFoundWin->_Size] = hwnd;
    pFoundWin->_Size++;
    return TRUE;
}

static HWND FindAltTabWin()
{
    SFoundWin foundWin;
    foundWin._Size = 0;
    EnumDesktopWindows(NULL, FindAltTabProc, (LPARAM)&foundWin);
    if (foundWin._Size)
        return foundWin._Data[0];
    return 0;
}

static BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID;
    GetWindowThreadProcessId(hwnd, &PID);
    SWinGroupArr* winAppGroupArr = (SWinGroupArr*)(lParam);
    SWinGroup* group = NULL;
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    for (uint32_t i = 0; i < winAppGroupArr->_Size; i++)
    {
        SWinGroup* const _group = &(winAppGroupArr->_Data[i]);
        if (!strcmp(_group->_ModuleFileName, moduleFileName))
        {
            group = _group;
            break;
        }
    }
    if (group == NULL)
    {
        group = &winAppGroupArr->_Data[winAppGroupArr->_Size++];
        strcpy(group->_ModuleFileName, moduleFileName);
        group->_Icon = NULL;
        const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, PID);
        // Icon
        {
            static char pathStr[512];
            GetModuleFileNameEx(process, NULL, pathStr, 512);
            if (!process)
                group->_Icon = LoadIcon(NULL, IDI_APPLICATION);
            else
                group->_Icon = ExtractIcon(process, pathStr, 0);
            if (!group->_Icon)
                group->_Icon = LoadIcon(NULL, IDI_APPLICATION);
            CloseHandle(process);
        }
    }
    group->_Windows[group->_WindowCount++] = hwnd;
    return true;
}

static BOOL FillCurrentWinGroup(HWND hwnd, LPARAM lParam)
{
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID;
    GetWindowThreadProcessId(hwnd, &PID);
    SWinGroup* currentWinGroup = (SWinGroup*)(lParam);
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    if (strcmp(moduleFileName, currentWinGroup->_ModuleFileName))
        return true;
    currentWinGroup->_Windows[currentWinGroup->_WindowCount] = hwnd;
    currentWinGroup->_WindowCount++;
    return true;
}

static void FitWindow(HWND hwnd, uint32_t iconCount)
{
    const int centerY = GetSystemMetrics(SM_CYSCREEN) / 2;
    const int centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    const uint32_t iconContainerSize = GetSystemMetrics(SM_CXICONSPACING);
    const uint32_t sizeX = iconCount * iconContainerSize;
    const uint32_t halfSizeX = sizeX / 2;
    const uint32_t sizeY = 1 * iconContainerSize;
    const uint32_t halfSizeY = sizeY / 2;

    POINT p;
    p.x = centerX-halfSizeX;
    p.y = centerY-halfSizeY;

    SetWindowPos(hwnd, 0, p.x, p.y, sizeX, sizeY, SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
}

static void InitializeSwitchApp(SAppData* pAppData)
{
    SWinGroupArr* pWinGroups = &(pAppData->_WinGroups);
    for (uint32_t i = 0; i < 64; i++)
    {
        pWinGroups->_Data[i]._WindowCount = 0;
    }
    pWinGroups->_Size = 0;
    EnumDesktopWindows(NULL, FillWinGroups, (LPARAM)pWinGroups);
    FitWindow(pAppData->_MainWin, pWinGroups->_Size);
    DisplayWindow(pAppData->_MainWin);
    pAppData->_Selection = 0;
    pAppData->_IsSwitchingApp = true;
}

static DWORD GetParentPID(DWORD PID)
{
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { 0 };
    pe.dwSize = sizeof(PROCESSENTRY32);
    WINBOOL found = Process32First(h, &pe);
    DWORD parentPID = 0;
    while (found)
    {
        if (pe.th32ProcessID == PID)
        {
            parentPID = pe.th32ParentProcessID;
            break;
        }
        found = Process32Next(h, &pe);
    }
    CloseHandle(h);
    return parentPID;
}

static void InitializeSwitchWin(SAppData* pAppData)
{
    HWND win = GetForegroundWindow();
    if (!win)
        return;
    while (true)
    {
        if (IsAltTabWindow(win))
            break;
        win = GetParent(win);
    }
    if (!win)
        return;
    DWORD PID;
    GetWindowThreadProcessId(win, &PID);
    SWinGroup* pWinGroup = &(pAppData->_CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->_ModuleFileName);
    pWinGroup->_WindowCount = 0;
    EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)pWinGroup);
    pAppData->_Selection = 0;
    pAppData->_IsSwitchingWin = true;
}

static void ApplySwitchApp(const SAppData* pAppData)
{
    const SWinGroup* group = &pAppData->_WinGroups._Data[pAppData->_Selection];
    // This would be nice to ha a "deferred show window"
    {
        for (int i = group->_WindowCount - 1; i >= 0 ; i--)
        {
            const HWND win = group->_Windows[i];
            WINDOWPLACEMENT placement;
            GetWindowPlacement(win, &placement);
            placement.length = sizeof(WINDOWPLACEMENT);
            if (placement.showCmd == SW_SHOWMINIMIZED)
                ShowWindow(win, SW_RESTORE);
        }
    }
    // Bringing window to top by setting HWND_TOPMOST, then HWND_NOTOPMOST
    // It feels hacky but this is most consistent solution I have found.
    const UINT winFlags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE;
    {
        HDWP winPosHandle = BeginDeferWindowPos(group->_WindowCount);
        for (int i = group->_WindowCount - 1; i >= 0 ; i--)
        {
            const HWND win = group->_Windows[i];
            winPosHandle = DeferWindowPos(winPosHandle, win, HWND_TOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }
    {
        HDWP winPosHandle = BeginDeferWindowPos(group->_WindowCount);
        for (int i = group->_WindowCount - 1; i >= 0 ; i--)
        {
            const HWND win = group->_Windows[i];
            winPosHandle = DeferWindowPos(winPosHandle, win, HWND_NOTOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }
    // Setting focus to the first window of the group
    VERIFY(ForceSetForeground(group->_Windows[0]));
}

static void ApplySwitchWin(const SAppData* pAppData)
{
    const HWND win = pAppData->_CurrentWinGroup._Windows[pAppData->_Selection];
    ForceSetForeground(win);
}

static void DeinitializeSwitchApp(SAppData* pAppData)
{
    HideWindow(pAppData->_MainWin);
    pAppData->_IsSwitchingApp = false;
}

static void DeinitializeSwitchWin(SAppData* pAppData)
{
    pAppData->_IsSwitchingWin = false;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    GdiplusStartupInput gdiplusStartupInput = {};
    gdiplusStartupInput.GdiplusVersion = 1;
    ULONG_PTR gdiplusToken = 0;
    uint32_t status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    VERIFY(!status);
    // Register the window class.
    const char CLASS_NAME[]  = "MacStyleSwitch";
    WNDCLASS wc = { };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.cbWndExtra = sizeof(SAppData*);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClass(&wc);
    // Create the window.
    const int yPos = GetSystemMetrics(SM_CYSCREEN) / 2;
    const int xPos = GetSystemMetrics(SM_CXSCREEN) / 2;
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_POPUPWINDOW | WS_BORDER, // Window style
        // Size and position
        0, 0, 0, 0,
        NULL, // Parent window
        NULL, // Menu
        hInstance,// Instance handle    
        NULL // Additional application data
    );
    VERIFY(hwnd);
    if (hwnd == NULL)
        return 0;

    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));

    _MainWin = hwnd; // Ugly. For keyboard hook.
    VERIFY(AllowSetForegroundWindow(GetCurrentProcessId()));

    HANDLE token;
    OpenProcessToken(
        GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
        &token);
    TOKEN_PRIVILEGES priv;
    priv.PrivilegeCount = 1;
    LookupPrivilegeValue(NULL, "SeDebugPrivilege", &(priv.Privileges[0].Luid));
    priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    VERIFY(AdjustTokenPrivileges(token, false, &priv, sizeof(priv), 0, 0));
    CloseHandle(token);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}

static void UpdateKeyState(SKeyState* pKeyState, uint32_t data)
{
    const bool isTab =      ((data >> 1) & 0x1) != 0x0;
    const bool isAlt =      ((data >> 2) & 0x1) != 0x0;
    const bool isShift =    ((data >> 3) & 0x1) != 0x0;
    const bool isTilde =    ((data >> 4) & 0x1) != 0x0;
    const bool releasing =  ((data >> 5) & 0x1) != 0x0;
    const bool prevTabDown = pKeyState->_TabDown;
    const bool prevAltDown = pKeyState->_AltDown;
    const bool prevTildeDown = pKeyState->_TildeDown;
    if (!releasing)
    {
        if (isTab)
            pKeyState->_TabDown = true;
        else if (isAlt)
            pKeyState->_AltDown = true;
        else if (isShift)
            pKeyState->_ShiftDown = true;
        else if (isTilde)
            pKeyState->_TildeDown = true;
    }
    else
    {
        if (isTab)
            pKeyState->_TabDown = false;
        else if (isAlt)
            pKeyState->_AltDown = false;
        else if (isShift)
            pKeyState->_ShiftDown = false;
        else if (isTilde)
            pKeyState->_TildeDown = false;
    }
    pKeyState->_TabNewInput = !prevTabDown && pKeyState->_TabDown;
    pKeyState->_TildeNewInput = !prevTildeDown && pKeyState->_TildeDown;
    pKeyState->_AltReleasing = prevAltDown && !pKeyState->_AltDown;
}

static int Modulo(int a, int b)
{
    return (a % b + b) % b;
}

static void ApplyState(SAppData* pAppData)
{
    const SWinGroupArr* winGroups = &(pAppData->_WinGroups);
    const SKeyState* pKeyState = &(pAppData->_KeyState);

    const bool switchAppInput =
        pAppData->_KeyState._TabNewInput &&
        pAppData->_KeyState._AltDown;
    const bool switchWinInput =
        pAppData->_KeyState._TildeNewInput &&
        pAppData->_KeyState._AltDown;
    const int direction = pAppData->_KeyState._ShiftDown ? -1 : 1;

    // Denit.
    if (pAppData->_IsSwitchingApp && (pAppData->_KeyState._AltReleasing || switchWinInput))
    {
        ApplySwitchApp(pAppData);
        DeinitializeSwitchApp(pAppData);
    }
    else if (pAppData->_IsSwitchingWin && (pAppData->_KeyState._AltReleasing || switchAppInput))
    {
        DeinitializeSwitchWin(pAppData);
    }

    // Init. / process action
    if (switchAppInput)
    {
        if (!pAppData->_IsSwitchingApp)
            InitializeSwitchApp(pAppData);
        pAppData->_Selection += direction;
        pAppData->_Selection = Modulo(pAppData->_Selection, pAppData->_WinGroups._Size);
        InvalidateRect(pAppData->_MainWin, 0, TRUE);
    }
    else if (switchWinInput)
    {
        if (!pAppData->_IsSwitchingWin)
            InitializeSwitchWin(pAppData);
        pAppData->_Selection += direction;
        pAppData->_Selection = Modulo(pAppData->_Selection, pAppData->_CurrentWinGroup._WindowCount);
        ApplySwitchWin(pAppData);
    }

    _IsSwitchActive = pAppData->_IsSwitchingApp || pAppData->_IsSwitchingWin;
}

LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    const bool isTab = kbStrut.vkCode == VK_TAB;
    const bool isAlt = kbStrut.vkCode == VK_LMENU;
    const bool isShift = kbStrut.vkCode == VK_LSHIFT;
    const bool releasing = kbStrut.flags & LLKHF_UP;
    const bool altDown = kbStrut.flags & LLKHF_ALTDOWN;
    const bool isTilde = kbStrut.vkCode == 192;

    const uint32_t data =
        (isTab & 0x1)       << 1 |
        (isAlt & 0x1)       << 2 |
        (isShift & 0x1)     << 3 |
        (isTilde & 0x1)     << 4 |
        (releasing & 0x1)   << 5;
    SendMessage(_MainWin, WM_APP, (*(WPARAM*)(&data)), 0);
    const bool bypassMsg =
        (isTab || isTilde) && altDown || // Bypass normal alt - tab
        _IsSwitchActive && (altDown || isShift); // Bypass keyboard language shortcut
    if (bypassMsg)
        return 1;
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void DrawRoundedRect(GpGraphics* pGraphics, GpPen* pPen, GpBrush* pBrush, uint32_t l, uint32_t t, uint32_t r, uint32_t d, uint32_t di)
{
    const uint32_t sX = t - d;
    const uint32_t sY = r - l;
    const uint32_t osX = (sX / 2) + d;
    const uint32_t osY = (sY / 2) + d;
    GpPath* pPath;
    GdipCreatePath(0, &pPath);
    GdipAddPathArcI(pPath, l, t, di, di, 180, 90);
    GdipAddPathArcI(pPath, r - di, t, di, di, 270, 90);
    GdipAddPathArcI(pPath, r - di, d - di, di, di, 360, 90);
    GdipAddPathArcI(pPath, l, d - di, di, di, 90, 90);
    GdipAddPathLineI(pPath, l, d - di / 2, l, t + di / 2);
    GdipClosePathFigure(pPath);
    if (pBrush)
        GdipFillPath(pGraphics, pBrush, pPath);
    if (pPen)
        GdipDrawPath(pGraphics, pPen, pPath);

    GdipDeletePath(pPath);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SAppData* pAppData = (SAppData*)GetWindowLongPtr(hwnd, 0);

    switch (uMsg)
    {
    case WM_NCCREATE:
        pAppData = malloc(sizeof(SAppData));
        pAppData->_IsSwitchingApp = false;
        pAppData->_IsSwitchingWin = false;
        pAppData->_MainWin = hwnd;
        pAppData->_Selection = 0;
        pAppData->_WinGroups._Size = 0;
        pAppData->_KeyState._TabDown = false;
        pAppData->_KeyState._TildeDown = false;
        pAppData->_KeyState._AltDown = false;
        pAppData->_KeyState._ShiftDown = false;
        pAppData->_KeyState._TabNewInput = false;
        pAppData->_KeyState._TildeNewInput = false;
        pAppData->_KeyState._AltReleasing = false;
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)pAppData);
        VERIFY(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
        return TRUE;
   case WM_DESTROY:
        free(pAppData);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);//GetWindowDC(hwnd);
        GpGraphics* pGraphics;
        GdipCreateFromHWND(hwnd,&pGraphics);
        GdipSetSmoothingMode(pGraphics, 5);
        RECT clientRect;
        GetClientRect (hwnd, &clientRect);
        const uint32_t iconContainerSize = GetSystemMetrics(SM_CXICONSPACING);
        const uint32_t iconSize = GetSystemMetrics(SM_CXICON) ;
        const uint32_t padding = (iconContainerSize - iconSize) / 2;
        uint32_t x = padding;
        for (uint32_t i = 0; i < pAppData->_WinGroups._Size; i++)
        {
            if (i == pAppData->_Selection)
            {
                COLORREF cr = GetSysColor(COLOR_WINDOWFRAME);
                ARGB gdipColor = cr | 0xFF000000;
                GpPen* pPen;
                GdipCreatePen1(gdipColor, 3, 2, &pPen);
                RECT rect = {x - padding / 2, padding / 2, x + iconSize + padding/2, padding + iconSize + padding/2 };
                DrawRoundedRect(pGraphics, pPen, NULL, rect.left, rect.top, rect.right, rect.bottom, 10);
                GdipDeletePen(pPen);
            }
            DrawIcon(hdc, x, padding, pAppData->_WinGroups._Data[i]._Icon);
            {
                GpFontFamily* pFontFamily;
                GpStringFormat* pGenericFormat;
                GpStringFormat* pFormat;
                VERIFY(!GdipStringFormatGetGenericDefault(&pGenericFormat));
                VERIFY(!GdipCloneStringFormat(pGenericFormat, &pFormat));
                VERIFY(!GdipSetStringFormatAlign(pFormat, StringAlignmentCenter));
                VERIFY(!GdipSetStringFormatLineAlign(pFormat, StringAlignmentCenter));
                VERIFY(!GdipGetGenericFontFamilySansSerif(&pFontFamily));
                GpFont* pFont;
                const uint32_t fontSize = 10;
                VERIFY(!GdipCreateFont(pFontFamily, fontSize, FontStyleBold, MetafileFrameUnitPixel, &pFont));
                GpSolidFill* pBrushText;
                GpSolidFill* pBrushBg;
                ARGB colorBg = GetSysColor(COLOR_WINDOWFRAME) | 0xFF000000;
                ARGB colorText = GetSysColor(COLOR_WINDOW) | 0xFF000000;
                VERIFY(!GdipCreateSolidFill(colorBg, &pBrushBg));
                VERIFY(!GdipCreateSolidFill(colorText, &pBrushText));
                WCHAR count[4];
                const uint32_t winCount = pAppData->_WinGroups._Data[i]._WindowCount;
                const uint32_t digitsCount = winCount > 99 ? 3 : winCount > 9 ? 2 : 1;
                const uint32_t width = digitsCount * (uint32_t)(0.7 * (float)fontSize) + 5;
                const uint32_t height = (fontSize + 4);
                RectF rectf = { x + iconSize + padding/2 - width - 3, padding + iconSize + padding/2 - height - 3, width, height };
                swprintf(count, 4, L"%i", winCount);
                DrawRoundedRect(pGraphics, NULL, pBrushBg, rectf.X, rectf.Y, rectf.X + rectf.Width, rectf.Y + rectf.Height, 5);
                VERIFY(!GdipDrawString(pGraphics, count, digitsCount, pFont, &rectf, pFormat, pBrushText));
                GdipDeleteFont(pFont);
                GdipDeleteBrush(pBrushText);
                GdipDeleteBrush(pBrushBg);
                GdipDeleteStringFormat(pFormat);
            }
            x += iconContainerSize;
        }
        EndPaint(hwnd, &ps);
        GdipDeleteGraphics(pGraphics);
        return 0;
    }
    case WM_APP:
    {
        UpdateKeyState(&pAppData->_KeyState, *((uint32_t*)&wParam));
        ApplyState(pAppData);
        return 0;
    }
    case WM_KEYDOWN:
    {
        return 0;
    }
    /*
    case WM_ERASEBKGND:
    {
        return 0;
    }*/
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}