#include <minwindef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windef.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winnt.h>
#include <winuser.h>
#include <processthreadsapi.h>
#include <gdiplus.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <stdlib.h>
#include <windowsx.h>
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

typedef struct SWinArr
{
    uint32_t _Size;
    HWND* _Data;
} SWinArr;

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

typedef enum Mode
{
    ModeNone,
    ModeApp,
    ModeWin
} Mode;

typedef struct SUWPIconMapElement
{
    wchar_t _UserModelID[256];
    wchar_t _Icon[256];
} SUWPIconMapElement;

typedef struct SUWPIconMap
{
    SUWPIconMapElement _Data[512];
    uint32_t _Count;
} SUWPIconMap;

typedef struct SAppData
{
    HWND _MainWin;
    HINSTANCE _Instance;
    Mode _Mode;
    int _Selection;
    SGraphicsResources _GraphicsResources;
    SWinGroupArr _WinGroups;
    SWinGroup _CurrentWinGroup;
    DWORD _MainThread;
    DWORD _WorkerThread;
    SUWPIconMap _UWPIconMap;
} SAppData;

typedef struct SFoundWin
{
    HWND _Data[64];
    uint32_t _Size;
} SFoundWin;

static SAppData _AppData;
static KeyConfig _KeyConfig;
static bool _Mouse = true;

// Main thread
#define MSG_INIT_WIN (WM_USER + 1)
#define MSG_INIT_APP (WM_USER + 2)
#define MSG_NEXT_WIN (WM_USER + 3)
#define MSG_NEXT_APP (WM_USER + 4)
#define MSG_PREV_WIN (WM_USER + 5)
#define MSG_PREV_APP (WM_USER + 6)
#define MSG_DEINIT_WIN (WM_USER + 7)
#define MSG_DEINIT_APP (WM_USER + 8)
// Worker thread
#define MSG_SET_APP (WM_USER + 9)
#define MSG_SET_WIN (WM_USER + 10)

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

static int Modulo(int a, int b)
{
    return (a % b + b) % b;
}

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
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD FGWThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    AttachThreadInput(currentThread, FGWThread, TRUE);
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED)
        ShowWindowAsync(win, SW_RESTORE);
    VERIFY(BringWindowToTop(win) || SetForegroundWindow(win));
    SetFocus(win);
    SetActiveWindow(win);
    EnableWindow(win, TRUE);
    AttachThreadInput(currentThread, FGWThread, FALSE);
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

static void InitUWPIconMap(SUWPIconMap* map)
{
    map->_Count = 0;

    HKEY classesKey;
    RegOpenKeyEx(HKEY_CLASSES_ROOT,
        NULL,
        0,
        KEY_READ,
        &classesKey);

    //TCHAR    achClass[MAX_PATH] = "";  // buffer for class name 
    //DWORD    cchClassName = MAX_PATH;  // size of class string 
    DWORD    cSubKeys = 0;               // number of subkeys 
    //DWORD    cValues = 0;              // number of values for key 

    // Get the class name and the value count.
    RegQueryInfoKey(
        classesKey,               // key handle 
        NULL,                // buffer for class name 
        NULL,           // size of class string 
        NULL,                    // reserved 
        &cSubKeys,               // number of subkeys 
        NULL,            // longest subkey size 
        NULL,            // longest class string 
        NULL,                // number of values for this key 
        NULL,            // longest value name 
        NULL,         // longest value data 
        NULL,   // security descriptor 
        NULL);       // last write time 

    for (uint32_t i = 0; i < cSubKeys; i++)
    {
        char className[512] = "";
        uint32_t classNameSize = 512;
        RegEnumKeyEx(classesKey,
                i,
                className,
                (LPDWORD)&classNameSize,
                NULL,
                NULL,
                NULL,
                NULL);

        strcat(className, "\\Application");

        {
            HKEY appKey = NULL;
            RegOpenKeyEx(classesKey,
                className,
                0,
                KEY_READ,
                &appKey);
            if (appKey == NULL) //check returned value instead ?
            {
                continue;
            }

            uint32_t valCount = 0;
            RegQueryInfoKey(
                appKey,
                NULL,
                NULL,
                NULL, NULL, NULL, NULL,
                (DWORD*)&valCount,
                NULL, NULL, NULL, NULL);

            bool hasIcon = false;
            bool hasUserModelID = false;
            for (uint32_t k = 0; k < valCount; k++)
            {
                char name[512] = "";
                uint32_t nameSize = 512;
                char value[512] = "";
                uint32_t valueSize = 512;
                RegEnumValue(
                    appKey,
                    k,
                    name,
                    (DWORD*)&nameSize,
                    NULL, NULL,
                    (BYTE*)value,
                    (DWORD*)&valueSize);
                if (!lstrcmpiA(name, "ApplicationIcon") && valueSize > 0)
                {
                    // printf("indirect string: %s \n", value);
                    wchar_t indirectStr[512];
                    mbstowcs(indirectStr, value, valueSize);
                    SHLoadIndirectString(indirectStr, map->_Data[map->_Count]._Icon, 512 * sizeof(wchar_t), NULL);
                    hasIcon = true;
                }
                if (!lstrcmpiA(name, "AppUserModelID") && valueSize > 0)
                {
                    mbstowcs(map->_Data[map->_Count]._UserModelID, value, valueSize);
                    hasUserModelID = true;
                }
            }
            if (hasIcon && hasUserModelID)
                map->_Count++;
            RegCloseKey(appKey);
        }
    }

    // for each ProgIDs entry, add a pair {packagename, icon path} to an array
    // https://learn.microsoft.com/en-us/windows/win32/sysinfo/enumerating-registry-subkeys
    // package name is the first non default name of the key
    // icon can be found at Computer\HKEY_CLASSES_ROOT\ProgID\DefaultIcon (first value)
    // and actual icon path loaded from SHLoadIndirectString(indirStr, iconPath, 512 * sizeof(wchar_t), NULL);
}

static void GetUWPIcon(HANDLE process, wchar_t* iconPath)
{
    static wchar_t userModelID[256];
    uint32_t userModelIDLength = 256;
    {
        //GetPackageFullName(process, &packageFullNameLength, packageFullName);
        GetApplicationUserModelId(process, &userModelIDLength, userModelID);
    }

    for (uint32_t i = 0; i < _AppData._UWPIconMap._Count; i++)
    {
        if (!lstrcmpiW(_AppData._UWPIconMap._Data[i]._UserModelID, userModelID))
        {
            wcscpy(iconPath, _AppData._UWPIconMap._Data[i]._Icon);
            return;
        }
    }
/*

    PACKAGE_INFO_REFERENCE inforef;
    OpenPackageInfoByFullName(packageFullName, 0, &inforef);
    PACKAGE_INFO infos[32];
    uint32_t length = sizeof(infos);
    uint32_t count = 1;
    GetPackageInfo(inforef, PACKAGE_FILTER_HEAD, &length, (BYTE*)&infos, &count);

    BYTE data[1024];
    uint32_t size = sizeof(data);
    uint32_t datacount = 1024;
    GetPackageApplicationIds(inforef, &size, data, &datacount);
    PACKAGE_ID packageID[512];
    uint32_t packageIDLength = sizeof(packageID);
    {
        LONG err = GetPackageId(process, &packageIDLength,(BYTE*)&packageID);
        printf("%i", (int)err);
    }
    {
        static wchar_t toto[512];
        uint32_t totolength = 512;
        GetApplicationUserModelId(process, &totolength, toto);
        printf("%ls", toto);
    }


    HKEY currentUserKey;
    LSTATUS toto = RegOpenCurrentUser(KEY_READ, &currentUserKey);
    (void)toto;
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
*/
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

static void ComputeWinPosAndSize(uint32_t iconCount, uint32_t* px, uint32_t* py, uint32_t* sx, uint32_t* sy)
{
    const int centerY = GetSystemMetrics(SM_CYSCREEN) / 2;
    const int centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    const uint32_t iconContainerSize = GetSystemMetrics(SM_CXICONSPACING);
    const uint32_t sizeX = iconCount * iconContainerSize;
    const uint32_t halfSizeX = sizeX / 2;
    const uint32_t sizeY = 1 * iconContainerSize;
    const uint32_t halfSizeY = sizeY / 2;
    *px = centerX-halfSizeX;
    *py = centerY-halfSizeY;
    *sx = sizeX;
    *sy = sizeY;
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

static const char CLASS_NAME[]  = "MacStyleSwitch";

static void DestroyWin()
{
    DestroyWindow(_AppData._MainWin);
    if (_AppData._GraphicsResources._DCBuffer)
    {
        DeleteDC(_AppData._GraphicsResources._DCBuffer);
        DeleteObject(_AppData._GraphicsResources._Bitmap);
        _AppData._GraphicsResources._DCBuffer = NULL;
        _AppData._GraphicsResources._Bitmap = NULL;
    }
    _AppData._MainWin = NULL;
}

static void CreateWin()
{
    if (_AppData._MainWin)
        DestroyWin();
    _AppData._GraphicsResources._DCDirty = true;
    uint32_t px, py, sx, sy;
    ComputeWinPosAndSize(_AppData._WinGroups._Size, &px, &py, &sx, &sy);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP | WS_VISIBLE, // Window style
        // Pos and size
        px, py, sx, sy,
        NULL, // Parent window
        NULL, // Menu
        _AppData._Instance, // Instance handle
        NULL // Additional application data
    );

    VERIFY(hwnd);
    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    _AppData._MainWin = hwnd;
    _AppData._GraphicsResources._DCDirty = true;
}

static void InitializeSwitchApp()
{
    SWinGroupArr* pWinGroups = &(_AppData._WinGroups);
    for (uint32_t i = 0; i < 64; i++)
    {
        pWinGroups->_Data[i]._WindowCount = 0;
    }
    pWinGroups->_Size = 0;
    EnumDesktopWindows(NULL, FillWinGroups, (LPARAM)pWinGroups);
    _AppData._Mode = ModeApp;
    _AppData._Selection = 0;
    CreateWin();
}

static void InitializeSwitchWin()
{
    HWND win = GetForegroundWindow();
    if (!win)
    {
        return;
    }
    while (true)
    {
        if (IsAltTabWindow(win))
            break;
        win = GetParent(win);
    }
    if (!win)
    {
        return;
    }
    DWORD PID;
    BOOL isUWP = false;
    FindActualPID(win, &PID, &isUWP);
    SWinGroup* pWinGroup = &(_AppData._CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->_ModuleFileName);
    pWinGroup->_WindowCount = 0;
    EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)pWinGroup);
    _AppData._Selection = 0;
    _AppData._Mode = ModeWin;
}
/*
static SWinArr* CreateWinArr(const SWinGroup* winGroup)
{
    SWinArr* winArr = malloc(sizeof(SWinArr));
    winArr->_Size = winGroup->_WindowCount;
    winArr->_Data = malloc(sizeof(HWND) * winGroup->_WindowCount);
    for (uint32_t i = 0; i < winGroup->_WindowCount; i++)
    {
        winArr->_Data[i] = _AppData._WinGroups._Data[_AppData._Selection]._Windows[i];
    }
    return winArr;
}
*/
static void ApplySwitchApp(const SWinGroup* winGroup)
{
    {
        for (int i = ((int)winGroup->_WindowCount) - 1; i >= 0 ; i--)
        {
            const HWND win = winGroup->_Windows[i];
            if (!IsWindow(win))
                continue;
            WINDOWPLACEMENT placement;
            GetWindowPlacement(win, &placement);
            placement.length = sizeof(WINDOWPLACEMENT);
            if (placement.showCmd == SW_SHOWMINIMIZED)
                ShowWindowAsync(win, SW_RESTORE);
        }
    }

    // Bringing window to top by setting HWND_TOPMOST, then HWND_NOTOPMOST
    // It feels hacky but this is most consistent solution I have found.

    const UINT winFlags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE;
    {
        HDWP winPosHandle = BeginDeferWindowPos(winGroup->_WindowCount);
        for (int i = ((int)winGroup->_WindowCount) - 1; i >= 0 ; i--)
        {
            const HWND win = winGroup->_Windows[i];
            if (!IsWindow(win))
                continue;
            DeferWindowPos(winPosHandle, win, HWND_TOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }

    Sleep(50);

    {
        HDWP winPosHandle = BeginDeferWindowPos(winGroup->_WindowCount);
        for (int i = ((int)winGroup->_WindowCount) - 1; i >= 0 ; i--)
        {
            const HWND win = winGroup->_Windows[i];
            if (!IsWindow(win))
                continue;
            DeferWindowPos(winPosHandle, win, HWND_NOTOPMOST, 0, 0, 0, 0, winFlags);
        }
        EndDeferWindowPos(winPosHandle);
    }
/*
    const UINT winFlags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE;
    {
        for (int i = ((int)winArr->_Size) - 1; i >= 0 ; i--)
        {
            const HWND win = winArr->_Data[i];
            if (!IsWindow(win))
                continue;
            SetWindowPos(win, _AppData._MainWin, 0, 0, 0, 0, winFlags);
            //BringWindowToTop(win);
        }
    }
*/
    // Setting focus to the first window of the group
    if (!IsWindow(winGroup->_Windows[0]))
    {
        return;
    }
    SetForegroundWindow(winGroup->_Windows[0]);
   // VERIFY(ForceSetForeground(winArr->_Data[0]));
}

static void ApplySwitchWin(HWND win)
{
  //  const UINT winFlags = SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOREPOSITION | SWP_NOACTIVATE;
    /*
    {
        HDWP winPosHandle = BeginDeferWindowPos(1);
        winPosHandle = DeferWindowPos(winPosHandle, win, HWND_TOPMOST, 0, 0, 0, 0, winFlags);
        EndDeferWindowPos(winPosHandle);
    }
    {
        HDWP winPosHandle = BeginDeferWindowPos(1);
        winPosHandle = DeferWindowPos(winPosHandle, win, HWND_NOTOPMOST, 0, 0, 0, 0, winFlags);
        EndDeferWindowPos(winPosHandle);
    }*/
    BringWindowToTop(win);
    SetForegroundWindow(win);
    //ForceSetForeground(win);
}

static void AttachToForeground()
{
    const DWORD FGWThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    AttachThreadInput(_AppData._WorkerThread, FGWThread, TRUE);
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

    static KeyState keyState =  { false, false, false, false, false };
    static Mode targetMode = ModeNone;

    const KeyState prevKeyState = keyState;

    const bool releasing = kbStrut.flags & LLKHF_UP;

    // Update keyState
    {
        if (isAppHold)
            keyState._HoldAppDown = !releasing;
        if (isAppSwitch)
            keyState._SwitchAppDown = !releasing;
        if (isWinHold)
            keyState._HoldWinDown = !releasing;
        if (isWinSwitch)
            keyState._SwitchWinDown = !releasing;
        if (isInvert)
            keyState._InvertKeyDown = !releasing;
    }

    // Update target app state
    bool bypassMsg = false;
    const Mode prevTargetMode = targetMode;
    {
        const bool switchWinInput = !prevKeyState._SwitchWinDown && keyState._SwitchWinDown;
        const bool switchAppInput = !prevKeyState._SwitchAppDown && keyState._SwitchAppDown;
        const bool winHoldReleasing = prevKeyState._HoldWinDown && !keyState._HoldWinDown;
        const bool appHoldReleasing = prevKeyState._HoldAppDown && !keyState._HoldAppDown;

        const bool switchApp =
            switchAppInput &&
            keyState._HoldAppDown;
        const bool switchWin =
            switchWinInput &&
            keyState._HoldWinDown;

        bool isApplying = false;

        // Denit.
        if (prevTargetMode == ModeApp &&
            (switchWin || appHoldReleasing))
        {
            targetMode = switchWinInput ? ModeWin : ModeNone;
            isApplying = true;
            PostThreadMessage(_AppData._MainThread, MSG_DEINIT_APP, 0, 0);
        }
        else if (prevTargetMode == ModeWin &&
            (switchApp || winHoldReleasing))
        {
            targetMode = switchAppInput ? ModeApp : ModeNone;
            isApplying = true;
            PostThreadMessage(_AppData._MainThread, MSG_DEINIT_WIN, 0, 0);
        }

        if (switchApp)
            targetMode = ModeApp;
        else if (switchWin)
            targetMode = ModeWin;

        if (targetMode == ModeApp && prevTargetMode != ModeApp)
        {
            PostThreadMessage(_AppData._MainThread, MSG_INIT_APP, 0, 0);
        }
        else if (targetMode == ModeWin && prevTargetMode != ModeWin)
        {
            PostThreadMessage(_AppData._MainThread, MSG_INIT_WIN, 0, 0);
        }

        if (switchApp)
        {
            targetMode = ModeApp;
            if (keyState._InvertKeyDown)
                PostThreadMessage(_AppData._MainThread, MSG_PREV_APP, 0, 0);
            else
                PostThreadMessage(_AppData._MainThread, MSG_NEXT_APP, 0, 0);
        }
        else if (switchWin)
        {
            targetMode = ModeWin;
            if (keyState._InvertKeyDown)
                PostThreadMessage(_AppData._MainThread, MSG_PREV_WIN, 0, 0);
            else
                PostThreadMessage(_AppData._MainThread, MSG_NEXT_WIN, 0, 0);
        }

        bypassMsg = 
            ((targetMode != ModeNone) || isApplying) &&
            (isWinSwitch || isAppSwitch || isWinHold || isAppHold || isInvert);
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
            const UINT uSent = SendInput(ARRAYSIZE(inputs), inputs, sizeof(INPUT));
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

static bool TryGetKey(const char* lineBuf, const char* token, DWORD* keyToSet)
{
    const char* pValue = strstr(lineBuf, token);
    if (pValue != NULL)
    {
        *keyToSet = KeyCodeFromConfigName(pValue + strlen(token) - 1);
        return true;
    }
    return false;
}

static bool TryGetBool(const char* lineBuf, const char* token, bool* boolToSet)
{
    const char* pValue = strstr(lineBuf, token);
    if (pValue != NULL)
    {
        if (strstr(pValue + strlen(token) - 1, "true") != NULL)
        {
            *boolToSet = true;
            return true;
        }
        else if (strstr(pValue + strlen(token) - 1, "false") != NULL)
        {
            *boolToSet = false;
            return true;
        }
    }
    return false;
}

static void SetKeyConfig()
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
            "// \n"
            "// Possible key bindings values: \n"
            "//     left alt\n"
            "//     right alt\n"
            "//     alt\n"
            "//     tilde\n"
            "//     left super (left windows)\n"
            "//     right super (right windows)\n"
            "//     left control\n"
            "//     left shift\n"
            "//     right shift\n"
            "//     tab\n"
            "\n"
            "app hold key: left alt\n"
            "app switch key: tab\n"
            "window hold key: left alt\n"
            "window switch key: tilde\n"
            "invert order key: left shift\n"
            "\n"
            "// Other options \n"
            "allow mouse: true \n");
        fclose(file);
        return;
    }

    static char lineBuf[1024];
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        if (TryGetKey(lineBuf, "app hold key: ", &_KeyConfig._AppHold))
            continue;
        if (TryGetKey(lineBuf, "app switch key: ", &_KeyConfig._AppSwitch))
            continue;
        if (TryGetKey(lineBuf, "window hold key: ", &_KeyConfig._WinHold))
            continue;
        if (TryGetKey(lineBuf, "window switch key: ", &_KeyConfig._WinSwitch))
            continue;
        if (TryGetKey(lineBuf, "invert order key: ", &_KeyConfig._Invert))
            continue;
        if (TryGetBool(lineBuf, "allow mouse: ", &_Mouse))
            continue;
    }
    fclose(file);
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static bool mouseInside = false;
    switch (uMsg)
    {
    case WM_MOUSELEAVE:
    {
        mouseInside = false;
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!_Mouse)
            return 0;
        if (!mouseInside)
        {
            // Mouse move event is fired on window show if the mouse is already in client area.
            // Skip first event so only actual move triggers mouse selection.
            mouseInside = true;
            return 0;
        }
        const int iconContainerSize = (int)GetSystemMetrics(SM_CXICONSPACING);
        const int posX = GET_X_LPARAM(lParam);
        _AppData._Selection = min(max(0, posX / iconContainerSize), (int)_AppData._WinGroups._Size);
        InvalidateRect(_AppData._MainWin, 0, FALSE);
        UpdateWindow(_AppData._MainWin);
        return 0;
    }
    case WM_CREATE:
    {
        mouseInside = false;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!mouseInside)
        {
            _AppData._Mode = ModeNone;
            DestroyWin();
            return 0;
        }
        if (!_Mouse)
            return 0;
        //SWinArr* winArr = CreateWinArr(&_AppData._WinGroups._Data[_AppData._Selection]);
        //PostThreadMessage(_AppData._WorkerThread, MSG_SET_APP, 0, (LPARAM)winArr);
        const int selection = _AppData._Selection;
        _AppData._Mode = ModeNone;
        _AppData._Selection = 0;
        DestroyWin();
        ApplySwitchApp(&_AppData._WinGroups._Data[selection]);
        return 0;
    }
    case WM_DESTROY:
        //PostQuitMessage(0);
        return 0;
    case WM_PAINT:
    {
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
        const uint32_t iconSize = GetSystemMetrics(SM_CXICON);
        const uint32_t padding = (iconContainerSize - iconSize) / 2;
        uint32_t x = padding;
        for (uint32_t i = 0; i < _AppData._WinGroups._Size; i++)
        {
            const SWinGroup* pWinGroup = &_AppData._WinGroups._Data[i];

            if (i == (uint32_t)_AppData._Selection)
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

/*
static void* HookCb(void* param)
{
    (void)param;
    VERIFY(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = { };

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {}

    return (void*)0;
}*/

/*
static void* WorkCB(void* param)
{
    ((SAppData*)param)->_WorkerThread = GetCurrentThreadId();
    (AllowSetForegroundWindow(GetCurrentProcessId()));
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        AttachToForeground();
        if (msg.message == MSG_SET_APP)
        {
            ApplySwitchApp((SWinArr*)msg.lParam);
        }
        else if (msg.message == MSG_SET_WIN)
        {
            ApplySwitchWin((HWND)msg.lParam);
        }
        AttachThreadInput(GetCurrentThreadId(), _AppData._MainThread, FALSE);
    }
    return (void*)0;
}
*/

static DWORD KbHookCb(LPVOID param)
{
    (void)param;
    VERIFY(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = { };

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {}

    return (DWORD)0;
}

/*
static DWORD WorkCb(LPVOID param)
{
    ((SAppData*)param)->_WorkerThread = GetCurrentThreadId();
    (AllowSetForegroundWindow(GetCurrentProcessId()));
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        AttachToForeground();
        if (msg.message == MSG_SET_APP)
        {
            ApplySwitchApp((SWinArr*)msg.lParam);
        }
        else if (msg.message == MSG_SET_WIN)
        {
            ApplySwitchWin((HWND)msg.lParam);
        }
        AttachThreadInput(GetCurrentThreadId(), _AppData._MainThread, FALSE);
    }
    return (DWORD)0;
}
*/

int StartMacAppSwitcher(HINSTANCE hInstance)
{
    ULONG_PTR gdiplusToken = 0;
    {
        GdiplusStartupInput gdiplusStartupInput = {};
        gdiplusStartupInput.GdiplusVersion = 1;
        uint32_t status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        VERIFY(!status);
    }

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc   = WindowProc;
        wc.hInstance     = _AppData._Instance;
        wc.lpszClassName = CLASS_NAME;
        wc.cbWndExtra = sizeof(SAppData*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }

    {
        _AppData._Mode = ModeNone;
        _AppData._Selection = 0;
        _AppData._MainWin = NULL;
        _AppData._Instance = hInstance;
        _AppData._WinGroups._Size = 0;
        _AppData._MainThread = GetCurrentThreadId();
        SetKeyConfig();
        InitGraphicsResources(&_AppData._GraphicsResources);
        InitUWPIconMap(&_AppData._UWPIconMap);
    }

    HANDLE threadKbHook = CreateRemoteThread(GetCurrentProcess(), NULL, 0, *KbHookCb, (void*)&_AppData, 0, NULL);
    (void)threadKbHook;
/*
    HANDLE threadWorker = CreateRemoteThread(GetCurrentProcess(), NULL, 0, *WorkCb, (void*)&_AppData, 0, NULL);
    (void)threadWorker;
*/
    (AllowSetForegroundWindow(GetCurrentProcessId()));

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
        switch (msg.message)
        {
        case MSG_INIT_APP:
        {
            InitializeSwitchApp();
            break;
        }
        case MSG_INIT_WIN:
        {
            InitializeSwitchWin();
            break;
        }
        case MSG_NEXT_APP:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchApp();
            _AppData._Selection++;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            break;
        }
        case MSG_PREV_APP:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchApp();
            _AppData._Selection--;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            break;
        }
        case MSG_NEXT_WIN:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchWin();
            _AppData._Selection++;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._CurrentWinGroup._WindowCount);

            HWND win = _AppData._CurrentWinGroup._Windows[_AppData._Selection];
            //PostThreadMessage(_AppData._WorkerThread, MSG_SET_WIN, 0, (LPARAM)win);
            ApplySwitchWin(win);

            break;
        }
        case MSG_PREV_WIN:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchWin();
            _AppData._Selection--;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._CurrentWinGroup._WindowCount);

            HWND win = _AppData._CurrentWinGroup._Windows[_AppData._Selection];
            //PostThreadMessage(_AppData._WorkerThread, MSG_SET_WIN, 0, (LPARAM)win);
            ApplySwitchWin(win);

            break;
        }
        case MSG_DEINIT_APP:
        {
            if (_AppData._Mode == ModeNone)
                break;

            // SWinArr* winArr = CreateWinArr(&_AppData._WinGroups._Data[_AppData._Selection]);
            //PostThreadMessage(_AppData._WorkerThread, MSG_SET_APP, 0, (LPARAM)winArr);

            const int selection = _AppData._Selection;
            _AppData._Mode = ModeNone;
            _AppData._Selection = 0;
            DestroyWin();

            ApplySwitchApp(&_AppData._WinGroups._Data[selection]);

            break;
        }
        case MSG_DEINIT_WIN:
        {
            if (_AppData._Mode == ModeNone)
                break;

            _AppData._Mode = ModeNone;
            _AppData._Selection = 0;
            break;
        }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    {
        DeInitGraphicsResources(&_AppData._GraphicsResources);
        if (_AppData._GraphicsResources._DCBuffer)
            DeleteDC(_AppData._GraphicsResources._DCBuffer);
        if (_AppData._GraphicsResources._Bitmap)
            DeleteObject(_AppData._GraphicsResources._Bitmap);
    }

    GdiplusShutdown(gdiplusToken);
    UnregisterClass(CLASS_NAME, hInstance);
    return 0;
}