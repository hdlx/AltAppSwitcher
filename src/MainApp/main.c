#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winuser.h>
#include "Common/MSSCommon.h"
#include <signal.h>
#include <ctype.h>
#include <processthreadsapi.h>
#include <gdiplus.h>

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct SWinGroup
{
    DWORD _PID;
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

void CALLBACK HandleWinEvent(   HWINEVENTHOOK hook, DWORD event, HWND hwnd,
                                LONG idObject, LONG idChild, 
                                DWORD dwEventThread, DWORD dwmsEventTime);

static void DisplayWindow(HWND win)
{
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
    //SetForegroundWindow(win);
    //SetForegroundWindow(WinHandle);
   // BringWindowToTop(WinHandle);
   // RedrawWindow(WinHandle, NULL, 0, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
  // SetWindowPos(WinHandle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOREPOSITION | SWP_SHOWWINDOW);
    //RedrawWindow(WinHandle, NULL, 0, RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void HideWindow(HWND win)
{
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
    //SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOREPOSITION);
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

static bool IsAltTabWindow(HWND hwnd)
{
    if (hwnd == GetShellWindow())   //Desktop
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

bool ForceSetForeground(HWND win)
{
    const DWORD dwCurrentThread = GetCurrentThreadId();
    const DWORD dwFGThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    AttachThreadInput(dwCurrentThread, dwFGThread, TRUE);
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED)
        ShowWindow(win, SW_RESTORE);
    // SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION);
    // SetWindowPos(win, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION);
    VERIFY(BringWindowToTop(win) || SetForegroundWindow(win));
   // SetCapture(win);
    SetFocus(win);
    SetActiveWindow(win);
    EnableWindow(win, TRUE);
    AttachThreadInput(dwCurrentThread, dwFGThread, FALSE);
    return true;
}

BOOL FindAltTabProc(HWND hwnd, LPARAM lParam)
{
    ALTTABINFO info = {};
    if (!GetAltTabInfo(hwnd, 0, &info, NULL, 0))
        return TRUE;
    SFoundWin* pFoundWin = (SFoundWin*)(lParam);
    pFoundWin->_Data[pFoundWin->_Size] = hwnd;
    pFoundWin->_Size++;
    return TRUE;
}

HWND FindAltTabWin()
{
    SFoundWin foundWin;
    foundWin._Size = 0;
    EnumDesktopWindows(NULL, FindAltTabProc, (LPARAM)&foundWin);
    if (foundWin._Size)
        return foundWin._Data[0];
    return 0;
}

BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    if (!IsAltTabWindow(hwnd))
        return true;

    DWORD dwPID;
    GetWindowThreadProcessId(hwnd, &dwPID);
    SWinGroupArr* winAppGroupArr = (SWinGroupArr*)(lParam);
    SWinGroup* group = NULL;
    for (uint32_t i = 0; i < winAppGroupArr->_Size; i++)
    {
        SWinGroup* const _group = &(winAppGroupArr->_Data[i]);
        if (_group->_PID == dwPID)
        {
            group = _group;
            break;
        }
    }
    if (group == NULL)
    {
        group = &winAppGroupArr->_Data[winAppGroupArr->_Size++];
        group->_PID = dwPID;
        {
            // Icon
            HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwPID);
            static char pathStr[512];
            GetModuleFileNameEx(process, NULL, pathStr, 512);
            if (!process)
                group->_Icon = LoadIcon(NULL, IDI_APPLICATION);
            else
                group->_Icon = ExtractIcon(process, pathStr, 0);
            CloseHandle(process);
        }
    }
    group->_Windows[group->_WindowCount++] = hwnd;
    return true;
}

BOOL FillCurrentWinGroup(HWND hwnd, LPARAM lParam)
{
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID;
    GetWindowThreadProcessId(hwnd, &PID);
    SWinGroup* currentWinGroup = (SWinGroup*)(lParam);
    if (PID != currentWinGroup->_PID)
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
 //  ScreenToClient(hwnd, &p);


    SetWindowPos(hwnd, 0, p.x, p.y, sizeX, sizeY, SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
}

static void InitializeSwitchApp(SAppData* pAppData)
{
    SWinGroupArr* pWinGroups = &(pAppData->_WinGroups);
    for (uint32_t i = 0; i < 64; i++)
    {
        pWinGroups->_Data[i]._WindowCount = 0;
        pWinGroups->_Data[i]._PID = 0xFFFFFFFF;
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
/*
    DWORD currentPID = GetWindowThreadProcessId(fgWin, NULL);
    VERIFY(currentPID);*/
    while (true)
    {
        if (IsAltTabWindow(win))
            break;
        win = GetParent(win);
    }
    if (!win)
        return;

    // static char buf[512];
    // GetClassName(fgWin, buf, 100);

    DWORD dwPID;
    GetWindowThreadProcessId(win, &dwPID);

    SWinGroup* pWinGroup = &(pAppData->_CurrentWinGroup);
    pWinGroup->_PID = dwPID;
    pWinGroup->_WindowCount = 0;
    EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)pWinGroup);
    pAppData->_Selection = 0;
    pAppData->_IsSwitchingWin = true;
}

static void ApplySwitchApp(const SAppData* pAppData)
{
    const SWinGroup* group = &pAppData->_WinGroups._Data[pAppData->_Selection];
    for (int i = group->_WindowCount - 1; i >= 0 ; i--)
    {
        HWND win = group->_Windows[i];
        VERIFY(ForceSetForeground(win));
    }
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
        WS_EX_TOOLWINDOW, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_POPUP, // Window style
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

    // Values from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));

    _MainWin = hwnd; // Ugly. For keyboard hook.
    VERIFY(AllowSetForegroundWindow(GetCurrentProcessId()));
    // VERIFY(SetForegroundWindow(hwnd));

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

static void DoStuff(SAppData* pAppData)
{
    const SWinGroupArr* winGroups = &(pAppData->_WinGroups);
    const SKeyState* pKeyState = &(pAppData->_KeyState);

    const bool switchAppInput =
        pAppData->_KeyState._TabNewInput &&
        pAppData->_KeyState._AltDown &&
        !pAppData->_IsSwitchingWin;
    const bool switchWinInput =
        pAppData->_KeyState._TildeNewInput &&
        pAppData->_KeyState._AltDown &&
        !pAppData->_IsSwitchingApp;
    const int direction = pAppData->_KeyState._ShiftDown ? -1 : 1;

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
    else if (pAppData->_KeyState._AltReleasing &&  pAppData->_IsSwitchingApp)
    {
        ApplySwitchApp(pAppData);
        DeinitializeSwitchApp(pAppData);
    }
    else if (pAppData->_KeyState._AltReleasing &&  pAppData->_IsSwitchingWin)
    {
        DeinitializeSwitchWin(pAppData);
    }
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

    //printf("HELLO %i \n", kbStrut.vkCode);
    const uint32_t data =
        (isTab & 0x1)       << 1 |
        (isAlt & 0x1)       << 2 |
        (isShift & 0x1)     << 3 |
        (isTilde & 0x1)     << 4 |
        (releasing & 0x1)   << 5;
    SendMessage(_MainWin, WM_APP, (*(WPARAM*)(&data)), 0);
    const bool bypassMsg = isTab && altDown;
    if (bypassMsg)
        return 1;
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void DrawRoundedRect(GpGraphics* pGraphics, GpPen* pPen, uint32_t l, uint32_t t, uint32_t r, uint32_t d)
{
    const uint32_t di = 10;
    const uint32_t sX = t - d;
    const uint32_t sY = r - l;
    const uint32_t osX = (sX / 2) + d;
    const uint32_t osY = (sY / 2) + d;

    GpPath* pPath;
    GdipCreatePath(0, &pPath);
   // GdipAddPathLineI(pPath, l, d - di,  l, t + di);
    GdipAddPathArcI(pPath, l, t, di, di, 180, 90);
    GdipAddPathArcI(pPath, r - di, t, di, di, 270, 90);
    GdipAddPathArcI(pPath, r - di, d - di, di, di, 360, 90);
    GdipAddPathArcI(pPath, l, d - di, di, di, 90, 90);

    GdipAddPathLineI(pPath, l, d - di / 2, l, t + di / 2);
    GdipClosePathFigure(pPath);

  // GdipAddPathLineI(pPath, l + di, t, r - di, t);
  // GdipAddPathLineI(pPath, r, t + di, r, d - di);
  // GdipAddPathLineI(pPath, r - di, d, l + di, d);

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
        /*
        static char nameStr[512];
        GetClassName(_AppData._WinGroups._Data[_AppData._Selection]._Windows[0], nameStr, 512);
        static char msgStr[512];
        sprintf(msgStr, "selection is: %s", nameStr);
        printf(msgStr);
        printf("\n");*/
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
        // HBRUSH brush = GetStockObject(LTGRAY_BRUSH);
        // FillRect(hdc, &clientRect, brush);
    //    SetBkMode(hdc, TRANSPARENT);
        //RoundRect(wdc, rect.left, rect.top, rect.right, rect.bottom, 15, 15);
        uint32_t x = padding;
        for (uint32_t i = 0; i < pAppData->_WinGroups._Size; i++)
        {
            if (i == pAppData->_Selection)
            {

                COLORREF cr = GetSysColor(COLOR_WINDOWFRAME);
                ARGB gdipColor = cr | 0xFF000000;
                // HBRUSH brush = CreateSolidBrush(GetSysColor(COLOR_HOTLIGHT));
                GpPen* pPen;
                GdipCreatePen1(gdipColor, 3, 2, &pPen);
                RECT rect = {x - padding / 2, padding / 2, x + iconSize + padding/2, padding + iconSize + padding/2 };
                DrawRoundedRect(pGraphics, pPen, rect.left, rect.top, rect.right, rect.bottom);
                GdipDeletePen(pPen);
                //GpRegion* pRegion;
                //GdipCreateRegionHrgn(region, &pRegion);

                //GpSolidFill* pBrush;
                //GdipCreateSolidFill(0xAA0000AA, &pBrush);

                //GdipFillRegion(pGraphics,pBrush,pRegion);
                //FillRgn(hdc, region, brush);
                //DrawFocusRect(hdc, &rect);
            }
            //RECT rect = {iconContainerSize * i, 0, iconContainerSize * (i + 1), iconContainerSize };
            //DrawFocusRect(hdc, &rect);

            DrawIcon(hdc, x, padding, pAppData->_WinGroups._Data[i]._Icon);
            x += iconContainerSize;
        }
        EndPaint(hwnd, &ps);
        GdipDeleteGraphics(pGraphics);
        return 0;
    }
    case WM_APP:
    {
        UpdateKeyState(&pAppData->_KeyState, *((uint32_t*)&wParam));
        DoStuff(pAppData);
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

/*
int main(int argc, char** argv){
    printf("Hey");
    return 0;
}*/