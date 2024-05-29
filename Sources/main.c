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
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <stdlib.h>

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

static void InitGraphicsResources(SGraphicsResources* pRes)
{
    pRes->_DCDirty = true;
    pRes->_DCBuffer = NULL;
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
        VERIFY(Ok == GdipCreateFont(pFontFamily, pRes->_FontSize, FontStyleBold, MetafileFrameUnitPixel, &pRes->_pFont));
    }
    // Brushes
    {
        VERIFY(Ok == GdipCreateSolidFill(GetSysColor(COLOR_WINDOWFRAME) | 0xFF000000, &pRes->_pBrushBg));
        VERIFY(Ok == GdipCreateSolidFill(GetSysColor(COLOR_WINDOW) | 0xFF000000, &pRes->_pBrushText));
    }
}

static void DeInitGraphicsResources(SGraphicsResources* pRes)
{
    pRes->_DCDirty = true;
    pRes->_DCBuffer = NULL;
    VERIFY(Ok == GdipDeleteBrush(pRes->_pBrushText));
    VERIFY(Ok == GdipDeleteBrush(pRes->_pBrushBg));
    VERIFY(Ok == GdipDeleteStringFormat(pRes->_pFormat));
}

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
    SGraphicsResources _GraphicsResources;
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
}

BOOL CALLBACK FindIMEWin(HWND hwnd, LPARAM lParam)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    if (strcmp("IME", className))
        return TRUE;
    (*(HWND*)lParam) = hwnd;
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
}


typedef struct SFindUWPChildParams
{
    DWORD OutUWPPID;
    DWORD InHostPID;
} SFindUWPChildParams;

BOOL FindUWPChild(HWND hwnd, LPARAM lParam)
{
    SFindUWPChildParams* pParams = (SFindUWPChildParams*)lParam;
    DWORD PID = 0;
    GetWindowThreadProcessId(hwnd, &PID);
    MyPrintWindow(hwnd);
    if (PID != pParams->InHostPID)
    {
        pParams->OutUWPPID = PID;
        return FALSE;
    }
/*
    // EnumChildWindows already enumerates recursively - Raymond Chen
    HWND childUWPWin = NULL;
    EnumChildWindows(hwnd, FindUWPChild0, (LPARAM)&childUWPWin);
    if (childUWPWin != NULL)
    {
        pParams->UWPWin = childUWPWin;
        return FALSE;
    }
*/
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

void GetUWPIcon(HANDLE process, wchar_t* iconPath)
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
    static wchar_t logoFullPath[512];

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
    MyPrintWindow(hwnd);
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID = 0;
    BOOL isUWP = false;

    FindActualPID(hwnd, &PID, &isUWP);
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    int32_t lastBackslash = 0;
    for (uint32_t i = 0; moduleFileName[i] != '\0'; i++)
    {
        if (moduleFileName[i] == '\\')
            lastBackslash = i;
    }
/*
    {
        wchar_t output[512];
        uint32_t size = 512;
        const HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        GetApplicationUserModelId(pr, &size, output);
        CloseHandle(pr);
    }*/ 
    {
        #if 0
        if (!strcmp(moduleFileName + lastBackslash + 1, "ApplicationFrameHost.exe"))
        {
            SendMessage(hwnd, WM_CHILDACTIVATE, 0, 0);

            isUWP = true;
            SFindUWPChildParams findUWPChildParams;
            findUWPChildParams.HostPID = PID;
            findUWPChildParams.UWPWin = NULL;
            EnumChildWindows(hwnd, FindUWPChild, (LPARAM)&findUWPChildParams);

            findUWPChildParams.UWPWin = FindWindowEx(hwnd, NULL, "Windows.UI.Core.CoreWindow", NULL);
            GetWindowThreadProcessId(findUWPChildParams.UWPWin, &PID);
            GetProcessFileName(PID, moduleFileName);
            if (findUWPChildParams.UWPWin)
            {
                wchar_t output[512];
                uint32_t size = 512;
                const HANDLE pr = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
                GetApplicationUserModelId(pr, &size, output);
                CloseHandle(pr);
                PrintLastError();
            }
        }
        #endif
    }
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

            if (!group->_Icon)
            {
                PrintLastError();
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
    pAppData->_GraphicsResources._DCDirty = true;
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
    BOOL isUWP = false;
    //GetWindowThreadProcessId(win, &PID);
    FindActualPID(win, &PID, &isUWP);
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
    // It would be nice to ha a "deferred show window"
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
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClass(&wc);
    // Create the window.
    const int yPos = GetSystemMetrics(SM_CYSCREEN) / 2;
    const int xPos = GetSystemMetrics(SM_CXSCREEN) / 2;
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP, // Window style
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

//SetWindowLong(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW);

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
        ((isTab || isTilde) && altDown && !releasing) || // Bypass normal alt - tab
        (_IsSwitchActive && altDown && isShift && !releasing); // Bypass keyboard language shortcut
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
        InitGraphicsResources(&pAppData->_GraphicsResources);
        SetWindowLongPtr(hwnd, 0, (LONG_PTR)pAppData);
        VERIFY(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
        return TRUE;
   case WM_DESTROY:
        DeInitGraphicsResources(&pAppData->_GraphicsResources);
        if (pAppData->_GraphicsResources._DCBuffer)
            DeleteDC(pAppData->_GraphicsResources._DCBuffer);
        if (pAppData->_GraphicsResources._Bitmap)
            DeleteObject(pAppData->_GraphicsResources._Bitmap);
        free(pAppData);
        PostQuitMessage(0);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        RECT clientRect;
        GetClientRect(hwnd, &clientRect);
        SGraphicsResources* pGraphRes = &pAppData->_GraphicsResources;
        if (pGraphRes->_DCDirty)
        {
            if (pGraphRes->_DCBuffer)
                DeleteDC(pGraphRes->_DCBuffer);
            pGraphRes->_DCBuffer = CreateCompatibleDC(ps.hdc);
            pGraphRes->_Bitmap = CreateCompatibleBitmap(
                ps.hdc,
                clientRect.right - clientRect.left,
                clientRect.bottom - clientRect.top);
            pGraphRes->_DCDirty = false;
        }
        HBITMAP oldBitmap = SelectObject(pGraphRes->_DCBuffer,  pGraphRes->_Bitmap);
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
        for (uint32_t i = 0; i < pAppData->_WinGroups._Size; i++)
        {
            const SWinGroup* pWinGroup = &pAppData->_WinGroups._Data[i];
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

            if (pWinGroup->_UWPIconPath[0] != L'\0')
            {
                GpImage* img = NULL;
                GdipLoadImageFromFile(pWinGroup->_UWPIconPath, &img);
                GdipDrawImageRectI(pGraphics, img, x, padding, iconSize, iconSize);
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
                RectF rectf = { x + iconSize + padding/2 - width - 3, padding + iconSize + padding/2 - height - 3, width, height };
                swprintf(count, 4, L"%i", winCount);
                DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushBg, rectf.X, rectf.Y, rectf.X + rectf.Width, rectf.Y + rectf.Height, 5);
                VERIFY(!GdipDrawString(pGraphics, count, digitsCount, pGraphRes->_pFont, &rectf, pGraphRes->_pFormat, pGraphRes->_pBrushText));
            }
            x += iconContainerSize;
        }
       // HDC hdc = BeginPaint(hwnd, &ps);
        BitBlt(ps.hdc, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, pGraphRes->_DCBuffer, 0, 0, SRCCOPY);
        GdipDeleteGraphics(pGraphics);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return (LRESULT)1;
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