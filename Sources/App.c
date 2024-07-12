#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winuser.h>
#include <processthreadsapi.h>
#include <gdiplus.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <stdlib.h>
#include <pthread.h>
#include "MacAppSwitcherHelpers.h"
#include "KeyCodeFromConfigName.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

typedef struct SWinGroup
{
    char _ModuleFileName[512];
    HWND _Windows[64];
    uint32_t _WindowCount;
    HICON _Icon;
    wchar_t _UWPIconPath[512];
} SWinGroup;

typedef struct SWinGroupArr
{
    SWinGroup _Data[64];
    uint32_t _Size;
} SWinGroupArr;

typedef struct KeyState
{
    bool _SwitchWinDown;
    bool _InvertKeyDown;
    bool _HoldWinDown;
    bool _HoldAppDown;
    bool _SwitchAppDown;
} KeyState;

typedef struct SGraphicsResources
{
    HDC _DCBuffer;
    bool _DCDirty;
    HBITMAP _Bitmap;
    uint32_t _FontSize;
    GpSolidFill* _pBrushText;
    GpSolidFill* _pBrushBg;
    GpFont* _pFont;
    GpStringFormat* _pFormat;
} SGraphicsResources;

typedef struct KeyConfig
{
    DWORD _AppHold;
    DWORD _AppSwitch;
    DWORD _WinHold;
    DWORD _WinSwitch;
    DWORD _Invert;
} KeyConfig;

typedef struct AppState
{
    bool _IsSwitchingApp;
    bool _IsSwitchingWin;
    int _Selection;
    SWinGroupArr _WinGroups;
    SWinGroup _CurrentWinGroup;
    pthread_mutex_t _MutexIsWriting;
    bool _IsWriting;
} AppState;

typedef enum Action
{
    ActionNone,
    ActionApplySwitchApp,
    ActionDeinitSwitchWin,
    ActionNextWin,
    ActionPrevWin,
    ActionNextApp,
    ActionPrevApp
} Action;

typedef struct SAppData
{
    HWND _MainWin;
    KeyState _KeyState;
    Action _Action;
    AppState _AppState;
    SGraphicsResources _GraphicsResources;
    bool _ActionInProgress;
    pthread_mutex_t _MutexActionInProgress;
    pthread_t _ProcessActionThread;
} SAppData;

typedef struct SFoundWin
{
    HWND _Data[64];
    uint32_t _Size;
} SFoundWin;

static SAppData _AppData;
static KeyConfig _KeyConfig;

static void InitGraphicsResources(SGraphicsResources* pRes)
{
    pRes->_DCDirty = true;
    pRes->_DCBuffer = NULL;
    pRes->_Bitmap = NULL;
    // Text
    {
        GpStringFormat* pGenericFormat;
        GpFontFamily* pFontFamily;
        VERIFY(Ok == GdipStringFormatGetGenericDefault(&pGenericFormat));
        VERIFY(Ok == GdipCloneStringFormat(pGenericFormat, &pRes->_pFormat));
        VERIFY(Ok == GdipSetStringFormatAlign(pRes->_pFormat, StringAlignmentCenter));
        VERIFY(Ok == GdipSetStringFormatLineAlign(pRes->_pFormat, StringAlignmentCenter));
        VERIFY(Ok == GdipGetGenericFontFamilySansSerif(&pFontFamily));
        pRes->_FontSize = 10;
        VERIFY(Ok == GdipCreateFont(pFontFamily, pRes->_FontSize, FontStyleBold, (int)MetafileFrameUnitPixel, &pRes->_pFont));
    }
    // Brushes
    {
        VERIFY(Ok == GdipCreateSolidFill(GetSysColor(COLOR_WINDOWFRAME) | 0xFF000000, &pRes->_pBrushBg));
        VERIFY(Ok == GdipCreateSolidFill(GetSysColor(COLOR_WINDOW) | 0xFF000000, &pRes->_pBrushText));
    }
}

static void DeInitGraphicsResources(SGraphicsResources* pRes)
{
    VERIFY(Ok == DeleteDC(pRes->_DCBuffer));
    VERIFY(Ok == DeleteObject(pRes->_Bitmap));
    VERIFY(Ok == GdipDeleteBrush(pRes->_pBrushText));
    VERIFY(Ok == GdipDeleteBrush(pRes->_pBrushBg));
    VERIFY(Ok == GdipDeleteStringFormat(pRes->_pFormat));
    VERIFY(Ok == GdipDeleteFont(pRes->_pFont));
    pRes->_DCDirty = true;
    pRes->_DCBuffer = NULL;
    pRes->_Bitmap = NULL;
}

static void DisplayWindow(HWND win)
{
    SetWindowPos(win, HWND_TOP, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
}

static void HideWindow(HWND win)
{
    SetWindowPos(win, HWND_TOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE);
}

static const char* WindowsClassNamesToSkip[] =
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
    return true;
}

static BOOL CALLBACK FindIMEWin(HWND hwnd, LPARAM lParam)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    if (strcmp("IME", className))
        return TRUE;
    (*(HWND*)lParam) = hwnd;
    return TRUE;
}

typedef struct SFindPIDEnumFnParams
{
    HWND InHostWindow;
    DWORD OutPID;
} SFindPIDEnumFnParams;

static BOOL FindPIDEnumFn(HWND hwnd, LPARAM lParam)
{
    SFindPIDEnumFnParams* pParams = (SFindPIDEnumFnParams*)lParam;
    static char className[512];
    GetClassName(hwnd, className, 512);
    if (strcmp("Windows.UI.Core.CoreWindow", className))
        return TRUE;

    DWORD PID = 0;
    DWORD TID = GetWindowThreadProcessId(hwnd, &PID);

    wchar_t UMI[512];
    BOOL isUWP = false;
    {
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        uint32_t size = 512;
        isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
    }
    if (!isUWP)
        return TRUE;

    HWND IMEWin = NULL;
    EnumThreadWindows(TID, FindIMEWin, (LPARAM)&IMEWin);
    if (IMEWin == NULL)
        return TRUE;

    HWND ownerWin = GetWindow(IMEWin, GW_OWNER);

    if (pParams->InHostWindow != ownerWin)
        return TRUE;

    pParams->OutPID = PID;

    return TRUE;
}

typedef struct SFindUWPChildParams
{
    DWORD OutUWPPID;
    DWORD InHostPID;
} SFindUWPChildParams;

static BOOL FindUWPChild(HWND hwnd, LPARAM lParam)
{
    SFindUWPChildParams* pParams = (SFindUWPChildParams*)lParam;
    DWORD PID = 0;
    GetWindowThreadProcessId(hwnd, &PID);
    // MyPrintWindow(hwnd);
    if (PID != pParams->InHostPID)
    {
        pParams->OutUWPPID = PID;
        return FALSE;
    }
    return TRUE;
}

static void FindActualPID(HWND hwnd, DWORD* PID, BOOL* isUWP)
{
    static char className[512];
    GetClassName(hwnd, className, 512);

    {
        wchar_t UMI[512];
        GetWindowThreadProcessId(hwnd, PID);
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *PID);
        uint32_t size = 512;
        *isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
        if (*isUWP)
        {
            return;
        }
    }

    if (!strcmp("ApplicationFrameWindow", className))
    {
        SFindUWPChildParams params;
        GetWindowThreadProcessId(hwnd,  &(params.InHostPID));
        params.OutUWPPID = 0;
        EnumChildWindows(hwnd, FindUWPChild, (LPARAM)&params);
        if (params.OutUWPPID != 0)
        {
            *PID = params.OutUWPPID;
            *isUWP = true;
            return;
        }
    }

    if (!strcmp("ApplicationFrameWindow", className))
    {
        SFindPIDEnumFnParams params;
        params.InHostWindow = hwnd;
        params.OutPID = 0;

        EnumDesktopWindows(NULL, FindPIDEnumFn, (LPARAM)&params);

        *PID = params.OutPID;
        *isUWP = true;
        return;
    }

    {
        GetWindowThreadProcessId(hwnd, PID);
        *isUWP = false;
        return;
    }
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
    GetClassName(hwnd, buf, 512);
    for (uint32_t i = 0; i < sizeof(WindowsClassNamesToSkip) / sizeof(WindowsClassNamesToSkip[0]); i++)
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

static void BuildLogoIndirectString(const wchar_t* logoPath, uint32_t logoPathLength,
    const wchar_t* packageFullName, uint32_t packageFullNameLength,
    const wchar_t* packageName, uint32_t packageNameLength,
    wchar_t* outStr)
{
    uint32_t i = 0;

    memcpy((void*)&outStr[i], (void*)L"@{", sizeof(L"@{"));
    i += (sizeof(L"@{") / sizeof(wchar_t)) - 1;

    memcpy((void*)&outStr[i], (void*)packageFullName, packageFullNameLength * sizeof(wchar_t));
    i += packageFullNameLength - 1;

    memcpy((void*)&outStr[i], (void*)L"?ms-resource://", sizeof(L"?ms-resource://"));
    i += (sizeof(L"?ms-resource://") / sizeof(wchar_t)) - 1;

    memcpy((void*)&outStr[i], (void*)packageName, packageNameLength * sizeof(wchar_t));
    i += packageNameLength - 1;

    memcpy((void*)&outStr[i], (void*)L"/Files/", sizeof(L"/Files/"));
    i += (sizeof(L"/Files/") / sizeof(wchar_t)) - 1;

    memcpy((void*)&outStr[i], (void*)logoPath, logoPathLength * sizeof(wchar_t));
    i += logoPathLength; // Does not count \0 unlike other length

    memcpy((void*)&outStr[i], (void*)L"}", sizeof(L"}"));
    i += (sizeof(L"}") / sizeof(wchar_t)) - 1;
}

static void GetUWPIcon(HANDLE process, wchar_t* iconPath)
{
    static wchar_t packageFullName[512];
    uint32_t packageFullNameLength = 512;
    {
        GetPackageFullName(process, &packageFullNameLength, packageFullName);
    }

    static wchar_t packageFamilyName[512];
    uint32_t packageFamilyNameLength = 512;
    uint32_t packageNameLength = 512;
    {
        GetPackageFamilyName(process, &packageFamilyNameLength, packageFamilyName);
        packageNameLength = packageFamilyNameLength - 14; // Publicsher id is always 13 chars. Also count underscore.
    }

    static wchar_t manifestPath[512];
    {
        uint32_t uiBufSize = 512;
        GetPackagePathByFullName(packageFullName, &uiBufSize, manifestPath);
        memcpy((void*)&manifestPath[uiBufSize - 1], (void*)L"\\AppxManifest.xml", sizeof(L"\\AppxManifest.xml"));
    }

    static wchar_t logoPath[512];
    uint32_t logoPathLength = 0;
    {
        static char _logoPath[512];
        FILE* file = _wfopen(manifestPath, L"r");
        static char lineBuf[1024];
        while (fgets(lineBuf, 1024, file))
        {
            const char* pLogo = strstr(lineBuf, "<Logo>");
            if (pLogo == NULL)
                continue;
            const char* pEnd = strstr(lineBuf, "</Logo>");
            const char* pToCopy = pLogo + sizeof("<Logo>") - 1;
            while (pToCopy != pEnd)
            {
                _logoPath[logoPathLength] = *pToCopy;
                pToCopy++;
                logoPathLength++;
            }
            break;
        }
        fclose(file);
        mbstowcs(logoPath, _logoPath, logoPathLength);
    }

    static wchar_t indirStr[512];

    BuildLogoIndirectString(logoPath, logoPathLength,
        packageFullName, packageFullNameLength,
        packageFamilyName, packageNameLength,
        indirStr);

    if (SHLoadIndirectString(indirStr, iconPath, 512 * sizeof(wchar_t), NULL) == S_OK)
        return;

    // Indirect string construction is empirically designed from
    // inspecting "resources.pri".
    // Some UWP app (arc) use "Application" instead of PackageName for
    // the resource uri. We look again using this alternate path.
    // This does not seem very robust.
    BuildLogoIndirectString(logoPath, logoPathLength,
        packageFullName, packageFullNameLength,
        L"Application", sizeof(L"Application") / sizeof(wchar_t),
        indirStr);
    SHLoadIndirectString(indirStr, iconPath, 512 * sizeof(wchar_t), NULL);
}

static BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    // MyPrintWindow(hwnd);
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID = 0;
    BOOL isUWP = false;

    FindActualPID(hwnd, &PID, &isUWP);
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);

    SWinGroupArr* winAppGroupArr = (SWinGroupArr*)(lParam);
    SWinGroup* group = NULL;
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
        group->_UWPIconPath[0] = L'\0';
        // Icon
        {
            const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, PID);
            static char pathStr[512];
            GetModuleFileNameEx(process, NULL, pathStr, 512);
            if (!process)
                group->_Icon = LoadIcon(NULL, IDI_APPLICATION);
            else
            {
                if (isUWP)
                    GetUWPIcon(process, group->_UWPIconPath);
                else
                    group->_Icon = ExtractIcon(process, pathStr, 0);
            }
            // also try :
            // https://stackoverflow.com/questions/55767277/how-do-i-set-a-taskbar-icon-for-win32-application

            if (!group->_Icon &&  group->_UWPIconPath[0] == L'\0')
            {
                // Probalby but not necessarily an error
                // PrintLastError();
                group->_Icon = LoadIcon(NULL, IDI_APPLICATION);
            }
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
    DWORD PID = 0;
    BOOL isUWP = false;
    FindActualPID(hwnd, &PID, &isUWP);
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
    SetWindowPos(hwnd, 0, p.x, p.y, sizeX, sizeY, SWP_NOOWNERZORDER);
}

static void InitializeSwitchApp()
{
    SWinGroupArr* pWinGroups = &(_AppData._AppState._WinGroups);
    for (uint32_t i = 0; i < 64; i++)
    {
        pWinGroups->_Data[i]._WindowCount = 0;
    }
    pWinGroups->_Size = 0;
    EnumDesktopWindows(NULL, FillWinGroups, (LPARAM)pWinGroups);
    FitWindow(_AppData._MainWin, pWinGroups->_Size);
    DisplayWindow(_AppData._MainWin);
    _AppData._GraphicsResources._DCDirty = true;
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

static void InitializeSwitchWin()
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
    BOOL isUWP = false;
    //GetWindowThreadProcessId(win, &PID);
    FindActualPID(win, &PID, &isUWP);
    SWinGroup* pWinGroup = &(_AppData._AppState._CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->_ModuleFileName);
    pWinGroup->_WindowCount = 0;
    EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)pWinGroup);
    _AppData._AppState._Selection = 0;
    _AppData._AppState._IsSwitchingWin = true;
}

static void ApplySwitchApp()
{
    const SWinGroup* group = &_AppData._AppState._WinGroups._Data[_AppData._AppState._Selection];
    // It would be nice to ha a "deferred show window"
    {
        for (int i = group->_WindowCount - 1; i >= 0 ; i--)
        {
            const HWND win = group->_Windows[i];
            if (!IsWindow(win))
                continue;
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
            if (!IsWindow(win))
                continue;
            winPosHandle = DeferWindowPos(winPosHandle, win, HWND_TOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }
    {
        HDWP winPosHandle = BeginDeferWindowPos(group->_WindowCount);
        for (int i = group->_WindowCount - 1; i >= 0 ; i--)
        {
            const HWND win = group->_Windows[i];
            if (!IsWindow(win))
                continue;
            winPosHandle = DeferWindowPos(winPosHandle, win, HWND_NOTOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }
    // Setting focus to the first window of the group
    if (!IsWindow(group->_Windows[0]))
        return;
    VERIFY(ForceSetForeground(group->_Windows[0]));
}

static void ApplySwitchWin()
{
    const HWND win = _AppData._AppState._CurrentWinGroup._Windows[_AppData._AppState._Selection];

    const UINT winFlags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE;
    {
        HDWP winPosHandle = BeginDeferWindowPos(1);
        winPosHandle = DeferWindowPos(winPosHandle, win, HWND_TOPMOST, 0, 0, 0, 0, winFlags);
        EndDeferWindowPos(winPosHandle);
    }
    {
        HDWP winPosHandle = BeginDeferWindowPos(1);
        winPosHandle = DeferWindowPos(winPosHandle, win, HWND_NOTOPMOST, 0, 0, 0, 0, winFlags);
        EndDeferWindowPos(winPosHandle);
    }
    ForceSetForeground(win);
}

static void DeinitializeSwitchApp()
{
    HideWindow(_AppData._MainWin);
    _AppData._AppState._IsSwitchingApp = false;
}

static void DeinitializeSwitchWin()
{
    _AppData._AppState._IsSwitchingWin = false;
}

int StartMacAppSwitcher(HINSTANCE hInstance)
{
    ULONG_PTR gdiplusToken = 0;
    {
        GdiplusStartupInput gdiplusStartupInput = {};
        gdiplusStartupInput.GdiplusVersion = 1;
        uint32_t status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        VERIFY(!status);
    }

    // Register the window class.
    const char CLASS_NAME[]  = "MacStyleSwitch";
    WNDCLASS wc = { };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.cbWndExtra = sizeof(SAppData*);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClass(&wc);

    // Create the window.
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP, // Window style
        // Size and position
        0, 0, 0, 0,
        NULL, // Parent window
        NULL, // Menu
        hInstance, // Instance handle
        NULL // Additional application data
    );
    VERIFY(hwnd);
    if (hwnd == NULL)
        return 0;

    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));

    _AppData._MainWin = hwnd; // Ugly. For keyboard hook.
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

static int Modulo(int a, int b)
{
    return (a % b + b) % b;
}

static void* ProcessAction(void* arg)
{
    (void)arg;
    pthread_mutex_lock(&_AppData._AppState._MutexIsWriting);
    _AppData._AppState._IsWriting = true;
    pthread_mutex_unlock(&_AppData._AppState._MutexIsWriting);

    // Denit.
    if (_AppData._Action == ActionApplySwitchApp)
    {
        ApplySwitchApp();
        DeinitializeSwitchApp();
    }
    else if (_AppData._Action == ActionDeinitSwitchWin)
    {
        DeinitializeSwitchWin();
    }
    else if (_AppData._Action == ActionNextApp ||
        _AppData._Action == ActionPrevApp)
    {
        if (!_AppData._AppState._IsSwitchingApp)
        {
            InitializeSwitchApp();
            _AppData._AppState._IsSwitchingApp = true;
        }
        _AppData._AppState._Selection += _AppData._Action == ActionNextApp ? 1 : -1;
        _AppData._AppState._Selection = Modulo(_AppData._AppState._Selection, _AppData._AppState._WinGroups._Size);
        InvalidateRect(_AppData._MainWin, 0, TRUE);
    }
    else if (_AppData._Action == ActionNextWin ||
        _AppData._Action == ActionPrevWin)
    {
        if (!_AppData._AppState._IsSwitchingWin)
        {
            InitializeSwitchWin();
            _AppData._AppState._IsSwitchingWin = true;
        }
        _AppData._AppState._Selection += _AppData._Action == ActionNextWin ? 1 : -1;
        _AppData._AppState._Selection = Modulo(_AppData._AppState._Selection, _AppData._AppState._CurrentWinGroup._WindowCount);
        ApplySwitchWin();
    }

    pthread_mutex_lock(&_AppData._AppState._MutexIsWriting);
    _AppData._AppState._IsWriting = false;
    pthread_mutex_unlock(&_AppData._AppState._MutexIsWriting);

    pthread_mutex_lock(&_AppData._MutexActionInProgress);
    _AppData._ActionInProgress = false;
    _AppData._Action = ActionNone;
    pthread_mutex_unlock(&_AppData._MutexActionInProgress);

    return (void*)0;
}

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    const bool isAppHold = kbStrut.vkCode == _KeyConfig._AppHold;
    const bool isAppSwitch = kbStrut.vkCode == _KeyConfig._AppSwitch;
    const bool isWinHold = kbStrut.vkCode == _KeyConfig._WinHold;
    const bool isWinSwitch = kbStrut.vkCode == _KeyConfig._WinSwitch;
    const bool isInvert = kbStrut.vkCode == _KeyConfig._Invert;
    const bool isTab = kbStrut.vkCode == VK_TAB;
    const bool isShift = kbStrut.vkCode == VK_LSHIFT;
    const bool isWatchedKey = 
        isAppHold ||
        isAppSwitch ||
        isWinHold ||
        isWinSwitch ||
        isInvert ||
        isTab ||
        isShift;

    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    {
        pthread_mutex_lock(&_AppData._MutexActionInProgress);
        if (_AppData._ActionInProgress)
        {
            pthread_mutex_unlock(&_AppData._MutexActionInProgress);
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        _AppData._ActionInProgress = true;
        pthread_mutex_unlock(&_AppData._MutexActionInProgress);
    }

    const bool releasing = kbStrut.flags & LLKHF_UP;
    const KeyState prevKeyState = _AppData._KeyState;

    // Update keyState
    {
        if (isAppHold)
            _AppData._KeyState._HoldAppDown = !releasing;
        if (isAppSwitch)
            _AppData._KeyState._SwitchAppDown = !releasing;
        if (isWinHold)
            _AppData._KeyState._HoldWinDown = !releasing;
        if (isWinSwitch)
            _AppData._KeyState._SwitchWinDown = !releasing;
        if (isInvert)
            _AppData._KeyState._InvertKeyDown = !releasing;
    }

    // Setup action
    {
        const bool switchWinInput = !prevKeyState._SwitchWinDown && _AppData._KeyState._SwitchWinDown;
        const bool switchAppInput = !prevKeyState._SwitchAppDown && _AppData._KeyState._SwitchAppDown;
        const bool winHoldReleasing = prevKeyState._HoldWinDown && !_AppData._KeyState._HoldWinDown;
        const bool appHoldReleasing = prevKeyState._HoldAppDown && !_AppData._KeyState._HoldAppDown;

        const bool switchAppAction =
            switchAppInput &&
            _AppData._KeyState._HoldAppDown;
        const bool switchWinAction =
            switchWinInput &&
            _AppData._KeyState._HoldWinDown;

        if (switchAppAction)
            _AppData._Action = _AppData._KeyState._InvertKeyDown ? ActionPrevApp : ActionNextApp;
        else if (switchWinAction && !_AppData._KeyState._InvertKeyDown)
            _AppData._Action = _AppData._KeyState._InvertKeyDown ? ActionPrevWin : ActionNextWin;
        else if (_AppData._AppState._IsSwitchingApp && (appHoldReleasing || switchWinInput))
            _AppData._Action = ActionApplySwitchApp;
        else if (_AppData._AppState._IsSwitchingWin && (winHoldReleasing || switchAppInput))
            _AppData._Action = ActionDeinitSwitchWin;

        if (_AppData._AppState._IsSwitchingApp &&
            (_AppData._Action == ActionNextWin ||
            _AppData._Action == ActionPrevWin))
            _AppData._Action = ActionApplySwitchApp;
    }

    const bool bypassMsg =
        ((_AppData._Action == ActionApplySwitchApp || _AppData._Action == ActionDeinitSwitchWin) &&
        (isWinSwitch || isAppSwitch || isWinHold || isAppHold || isInvert)) ||
        _AppData._Action == ActionPrevWin ||
        _AppData._Action == ActionNextWin ||
        _AppData._Action == ActionPrevApp ||
        _AppData._Action == ActionNextApp;

    if (_AppData._Action == ActionNone)
    {
        pthread_mutex_lock(&_AppData._MutexActionInProgress);
        _AppData._ActionInProgress = false;
        _AppData._Action = ActionNone;
        pthread_mutex_unlock(&_AppData._MutexActionInProgress);
    }
    else
    {
        pthread_create(&_AppData._ProcessActionThread, NULL, *ProcessAction, (void*)0);
    }

    if (bypassMsg)
    {
        // https://stackoverflow.com/questions/2914989/how-can-i-deal-with-depressed-windows-logo-key-when-using-sendinput
        if (releasing && (isWinHold || isAppHold || isInvert))
        {
            INPUT inputs[3] = {};
            ZeroMemory(inputs, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_RCONTROL;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = kbStrut.vkCode;
            inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[2].type = INPUT_KEYBOARD;
            inputs[2].ki.wVk = VK_RCONTROL;
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
            VERIFY(uSent == 3);
        }
        return 1;
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void DrawRoundedRect(GpGraphics* pGraphics, GpPen* pPen, GpBrush* pBrush, uint32_t l, uint32_t t, uint32_t r, uint32_t d, uint32_t di)
{
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

void SetKeyConfig()
{
    _KeyConfig._AppHold = VK_LMENU;
    _KeyConfig._AppSwitch = VK_TAB;
    _KeyConfig._WinHold = VK_LMENU;
    _KeyConfig._WinSwitch = VK_OEM_3;
    _KeyConfig._Invert = VK_LSHIFT;
    const char* configFile = "MacAppSwitcherConfig.txt";
    FILE* file = fopen(configFile ,"rb");
    if (file == NULL)
    {
        file = fopen(configFile ,"a");
        fprintf(file,
            "// MacAppSwitcher config file \n"
            "// Possible key bindings values: \n"
            "//     left alt\n"
            "//     right alt\n"
            "//     alt\n"
            "//     tilde\n"
            "//     left super (left windows)\n"
            "//     right super (right windows)\n"
            "//     left control\n"
            "//     right control\n"
            "//     left shift\n"
            "//     right shift\n"
            "//     tab\n"
            "\n"
            "app hold key: left alt\n"
            "app switch key: tab\n"
            "window hold key: left alt\n"
            "window switch key: tilde\n"
            "invert order key: left shift\n");
        fclose(file);
        return;
    }

    static char lineBuf[1024];
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        const char* pValue = NULL;
        #define TRY_GET_KEY(var, token) \
        pValue = strstr(lineBuf, token); \
        if (pValue != NULL) \
        { \
            var = KeyCodeFromConfigName(pValue + sizeof(token) - 1); \
            continue; \
        } \

        TRY_GET_KEY(_KeyConfig._AppHold, "app hold key: ")
        TRY_GET_KEY(_KeyConfig._AppSwitch, "app switch key: ")
        TRY_GET_KEY(_KeyConfig._WinHold, "window hold key: ")
        TRY_GET_KEY(_KeyConfig._WinSwitch, "window switch key: ")
        TRY_GET_KEY(_KeyConfig._Invert, "invert order key: ")
        #undef TRY_GET_KEY
    }
    fclose(file);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NCCREATE:
    {
        _AppData._AppState._IsSwitchingApp = false;
        _AppData._AppState._IsSwitchingWin = false;
        _AppData._MainWin = hwnd;
        _AppData._AppState._Selection = 0;
        _AppData._AppState._WinGroups._Size = 0;
        _AppData._AppState._IsWriting = PTHREAD_MUTEX_INITIALIZER;
        
        _AppData._AppState._MutexIsWriting = false;

        _AppData._KeyState._SwitchWinDown = false;
        _AppData._KeyState._SwitchAppDown = false;
        _AppData._KeyState._HoldWinDown = false;
        _AppData._KeyState._HoldAppDown = false;
        _AppData._KeyState._InvertKeyDown = false;
        _AppData._MutexActionInProgress = PTHREAD_MUTEX_INITIALIZER;
        _AppData._ActionInProgress = false;
        SetKeyConfig();
        InitGraphicsResources(&_AppData._GraphicsResources);
        VERIFY(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
        return TRUE;
    }
    case WM_NCDESTROY:
        DeInitGraphicsResources(&_AppData._GraphicsResources);
        if (_AppData._GraphicsResources._DCBuffer)
            DeleteDC(_AppData._GraphicsResources._DCBuffer);
        if (_AppData._GraphicsResources._Bitmap)
            DeleteObject(_AppData._GraphicsResources._Bitmap);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
    {
        pthread_mutex_lock(&_AppData._AppState._MutexIsWriting);
        const bool skip = _AppData._AppState._IsWriting = false;
        pthread_mutex_unlock(&_AppData._AppState._MutexIsWriting);
        if (skip)
            return 0;

        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        SGraphicsResources* pGraphRes = &_AppData._GraphicsResources;
        if (pGraphRes->_DCDirty)
        {
            if (pGraphRes->_DCBuffer)
            {
                DeleteDC(pGraphRes->_DCBuffer);
                DeleteObject(pGraphRes->_Bitmap);
            }
            pGraphRes->_DCBuffer = CreateCompatibleDC(ps.hdc);
            pGraphRes->_Bitmap = CreateCompatibleBitmap(
                ps.hdc,
                clientRect.right - clientRect.left,
                clientRect.bottom - clientRect.top);
            pGraphRes->_DCDirty = false;
        }
        HBITMAP oldBitmap = SelectObject(pGraphRes->_DCBuffer,  pGraphRes->_Bitmap);
        (void)oldBitmap;
        HBRUSH bgBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(pGraphRes->_DCBuffer, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(pGraphRes->_DCBuffer, TRANSPARENT);

        GpGraphics* pGraphics;
        GdipCreateFromHDC(pGraphRes->_DCBuffer, &pGraphics);
        GdipSetSmoothingMode(pGraphics, 5);

        const uint32_t iconContainerSize = GetSystemMetrics(SM_CXICONSPACING);
        const uint32_t iconSize = GetSystemMetrics(SM_CXICON) ;
        const uint32_t padding = (iconContainerSize - iconSize) / 2;
        uint32_t x = padding;
        for (uint32_t i = 0; i < _AppData._AppState._WinGroups._Size; i++)
        {
            const SWinGroup* pWinGroup = &_AppData._AppState._WinGroups._Data[i];
            if (i == (uint32_t)_AppData._AppState._Selection)
            {
                COLORREF cr = GetSysColor(COLOR_WINDOWFRAME);
                ARGB gdipColor = cr | 0xFF000000;
                GpPen* pPen;
                GdipCreatePen1(gdipColor, 3, 2, &pPen);
                RECT rect = {x - padding / 2, padding / 2, x + iconSize + padding/2, padding + iconSize + padding/2 };
                DrawRoundedRect(pGraphics, pPen, NULL, rect.left, rect.top, rect.right, rect.bottom, 10);
                GdipDeletePen(pPen);
            }

            if (pWinGroup->_UWPIconPath[0] != L'\0')
            {
                GpImage* img = NULL;
                GdipLoadImageFromFile(pWinGroup->_UWPIconPath, &img);
                GdipDrawImageRectI(pGraphics, img, x, padding, iconSize, iconSize);
                GdipDisposeImage(img);
            }
            else if (pWinGroup->_Icon)
            {
                DrawIcon(pGraphRes->_DCBuffer, x, padding, pWinGroup->_Icon);
            }

            {
                WCHAR count[4];
                const uint32_t winCount = pWinGroup->_WindowCount;
                const uint32_t digitsCount = winCount > 99 ? 3 : winCount > 9 ? 2 : 1;
                const uint32_t width = digitsCount * (uint32_t)(0.7 * (float)pGraphRes->_FontSize) + 5;
                const uint32_t height = (pGraphRes->_FontSize + 4);
                uint32_t rect[4] = {
                    x + iconSize + padding / 2 - width - 3, 
                    padding + iconSize + padding / 2 - height - 3,
                    width,
                    height };
                RectF rectf = { (float)rect[0], (float)rect[1], (float)rect[2], (float)rect[3] };
                swprintf(count, 4, L"%i", winCount);
                DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushBg, rectf.X, rectf.Y, rectf.X + rectf.Width, rectf.Y + rectf.Height, 5);
                VERIFY(!GdipDrawString(pGraphics, count, digitsCount, pGraphRes->_pFont, &rectf, pGraphRes->_pFormat, pGraphRes->_pBrushText));
            }
            x += iconContainerSize;
        }
        BitBlt(ps.hdc, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, pGraphRes->_DCBuffer, 0, 0, SRCCOPY);
        GdipDeleteGraphics(pGraphics);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return (LRESULT)0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
} 