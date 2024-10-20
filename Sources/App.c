#define COBJMACROS
#include <minwindef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <vsstyle.h>
#include <wchar.h>
#include <windef.h>
#include <windows.h>
#include <tlhelp32.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winnt.h>
#include <winscard.h>
#include <winuser.h>
#include <processthreadsapi.h>
#include <gdiplus.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <stdlib.h>
#include <windowsx.h>
#include <combaseapi.h>
#include <initguid.h>
#include <Shellapi.h>
#include <commoncontrols.h>
#include <Shobjidl.h>
#include <objidl.h>
#include <Unknwn.h>
#include "AppxPackaging.h"
#undef COBJMACROS
#include "AltAppSwitcherHelpers.h"
#include "KeyCodeFromConfigName.h"
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define MEM_INIT(ARG) memset(&ARG, 0,  sizeof(ARG))

typedef struct SWinGroup
{
    char _ModuleFileName[512];
    HWND _Windows[64];
    uint32_t _WindowCount;
    GpBitmap* _IconBitmap;
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
    bool _EscapeDown;
    bool _PrevAppDown;
} KeyState;

typedef struct SGraphicsResources
{
    uint32_t _FontSize;
    GpSolidFill* _pBrushText;
    GpSolidFill* _pBrushBg;
    GpSolidFill* _pBrushBgHighlight;
    GpFont* _pFont;
    GpStringFormat* _pFormat;
    COLORREF _BackgroundColor;
    COLORREF _HighlightBackgroundColor;
    COLORREF _TextColor;
    HBITMAP _Bitmap;
    HDC _DC;
    HIMAGELIST _ImageList;
    bool _LightTheme;
} SGraphicsResources;

typedef struct Metrics
{
    uint32_t _WinPosX;
    uint32_t _WinPosY;
    uint32_t _WinX;
    uint32_t _WinY;
    uint32_t _Icon;
    uint32_t _IconContainer;
} Metrics;

typedef struct KeyConfig
{
    DWORD _AppHold;
    DWORD _AppSwitch;
    DWORD _WinHold;
    DWORD _WinSwitch;
    DWORD _Invert;
    DWORD _PrevApp;
} KeyConfig;


typedef enum ThemeMode
{
    ThemeModeAuto,
    ThemeModeLight,
    ThemeModeDark
} ThemeMode;

typedef struct Config
{
    KeyConfig _Key;
    bool _Mouse;
    ThemeMode _ThemeMode;
    float _Scale;
} Config;

typedef enum Mode
{
    ModeNone,
    ModeApp,
    ModeWin
} Mode;

typedef struct SUWPIconMapElement
{
    wchar_t _UserModelID[512];
    wchar_t _Icon[MAX_PATH];
} SUWPIconMapElement;

#define UWPICONMAPSIZE 16
typedef struct SUWPIconMap
{
    SUWPIconMapElement _Data[UWPICONMAPSIZE];
    uint32_t _Head;
    uint32_t _Count;
} SUWPIconMap;

typedef struct SAppData
{
    HWND _MainWin;
    HINSTANCE _Instance;
    Mode _Mode;
    int _Selection;
    int _MouseSelection;
    SGraphicsResources _GraphicsResources;
    SWinGroupArr _WinGroups;
    SWinGroup _CurrentWinGroup;
    SUWPIconMap _UWPIconMap;
    Config _Config;
    Metrics _Metrics;
} SAppData;

typedef struct SFoundWin
{
    HWND _Data[64];
    uint32_t _Size;
} SFoundWin;

static const KeyConfig* _KeyConfig;
static DWORD _MainThread;

// Main thread
#define MSG_INIT_WIN (WM_USER + 1)
#define MSG_INIT_APP (WM_USER + 2)
#define MSG_NEXT_WIN (WM_USER + 3)
#define MSG_NEXT_APP (WM_USER + 4)
#define MSG_PREV_WIN (WM_USER + 5)
#define MSG_PREV_APP (WM_USER + 6)
#define MSG_DEINIT_WIN (WM_USER + 7)
#define MSG_DEINIT_APP (WM_USER + 8)
#define MSG_CANCEL_APP (WM_USER + 9)

static HIMAGELIST GetSysImgList()
{
    void* out = NULL;
    SHGetImageList(SHIL_JUMBO, &IID_IImageList, &out);
    return (HIMAGELIST)out;
}

static void InitGraphicsResources(SGraphicsResources* pRes, const Config* config)
{
    // Text
    {
        GpStringFormat* pGenericFormat;
        GpFontFamily* pFontFamily;
        ASSERT(Ok == GdipStringFormatGetGenericDefault(&pGenericFormat));
        ASSERT(Ok == GdipCloneStringFormat(pGenericFormat, &pRes->_pFormat));
        ASSERT(Ok == GdipSetStringFormatAlign(pRes->_pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipSetStringFormatLineAlign(pRes->_pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipGetGenericFontFamilySansSerif(&pFontFamily));
        pRes->_FontSize = 10;
        ASSERT(Ok == GdipCreateFont(pFontFamily, pRes->_FontSize, FontStyleBold, (int)MetafileFrameUnitPixel, &pRes->_pFont));
    }
    // Colors
    {
        bool lightTheme = true;
        if (config->_ThemeMode == ThemeModeAuto)
        {
            HKEY key;
            RegOpenKeyEx(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                0,
                KEY_READ,
                &key);

            DWORD valueCount = 0;
            RegQueryInfoKey(
                key,
                NULL, NULL, NULL,
                NULL,
                NULL, NULL, &valueCount, NULL, NULL, NULL, NULL);

            for (uint32_t i = 0; i < valueCount; i++)
            {
                char name[512] = "";
                DWORD nameSize = 512;
                DWORD value = 0;
                DWORD valueSize = sizeof(DWORD);

                RegEnumValue(
                        key,
                        i,
                        name,
                        &nameSize,
                        NULL, NULL,
                        (BYTE*)&value,
                        &valueSize);

                if (!lstrcmpiA(name, "AppsUseLightTheme") && nameSize > 0)
                {
                    lightTheme = value;
                    break;
                }
            }

            RegCloseKey(key);
        }
        else
        {
            lightTheme = config->_ThemeMode == ThemeModeLight;
        }

        // Colorref do not support alpha and high order bits MUST be 00
        // This is different from gdip "ARGB" type
        COLORREF darkColor = 0x002C2C2C;
        COLORREF lightColor = 0x00FFFFFF;
        pRes->_LightTheme = lightTheme;
        if (lightTheme)
        {
            pRes->_BackgroundColor = lightColor;
            pRes->_HighlightBackgroundColor = lightColor - 0x00131313;
            pRes->_TextColor = darkColor;
        }
        else
        {
            pRes->_BackgroundColor = darkColor;
            pRes->_HighlightBackgroundColor = darkColor + 0x00131313;
            pRes->_TextColor = lightColor;
        }
    }
    // Brushes
    {
        ASSERT(Ok == GdipCreateSolidFill(pRes->_BackgroundColor | 0xFF000000, &pRes->_pBrushBg));
        ASSERT(Ok == GdipCreateSolidFill(pRes->_HighlightBackgroundColor | 0xFF000000, &pRes->_pBrushBgHighlight));
        ASSERT(Ok == GdipCreateSolidFill(pRes->_TextColor | 0xFF000000, &pRes->_pBrushText));
    }
}

static void DeInitGraphicsResources(SGraphicsResources* pRes)
{
    ASSERT(Ok == GdipDeleteBrush(pRes->_pBrushText));
    ASSERT(Ok == GdipDeleteBrush(pRes->_pBrushBg));
    ASSERT(Ok == GdipDeleteBrush(pRes->_pBrushBgHighlight));
    ASSERT(Ok == GdipDeleteStringFormat(pRes->_pFormat));
    ASSERT(Ok == GdipDeleteFont(pRes->_pFont));
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
    ASSERT(BringWindowToTop(win) || SetForegroundWindow(win));
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

void ErrorDescription(HRESULT hr) 
{
     if(FACILITY_WINDOWS == HRESULT_FACILITY(hr)) 
         hr = HRESULT_CODE(hr); 
     TCHAR* szErrMsg; 

     if(FormatMessage( 
       FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM, 
       NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), 
       (LPTSTR)&szErrMsg, 0, NULL) != 0) 
     { 
         printf(TEXT("%s"), szErrMsg); 
         LocalFree(szErrMsg); 
     } else 
         printf( TEXT("[Could not find a description for error # %#x.]\n"), (int)hr); 
}

//https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/AppxPackingDescribeAppx/cpp/DescribeAppx.cpp
static void GetUWPIcon(HANDLE process, wchar_t* outIconPath, SAppData* appData)
{
    static wchar_t userModelID[512];
    {
        uint32_t length = 512;
        GetApplicationUserModelId(process, &length, userModelID);
    }

    SUWPIconMap* iconMap = &appData->_UWPIconMap;
    for (uint32_t i = 0; i < iconMap->_Count; i++)
    {
        const uint32_t i0 = Modulo(iconMap->_Head - 1 - i, UWPICONMAPSIZE) ;
        if (wcscmp(iconMap->_Data[i0]._UserModelID, userModelID))
            continue;
        wcscpy(outIconPath, iconMap->_Data[i0]._Icon);
        return;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    PACKAGE_ID pid[32];
    uint32_t pidSize = sizeof(pid);
    GetPackageId(process, &pidSize, (BYTE*)pid);
    static wchar_t packagePath[MAX_PATH];
    uint32_t packagePathLength = MAX_PATH;
    GetPackagePath(pid, 0, &packagePathLength, packagePath);
    static wchar_t manifestPath[MAX_PATH];
    wcscpy(manifestPath, packagePath);
    wcscat(manifestPath, L"/AppXManifest.xml");

    wchar_t* logoProp = NULL;
    {
        // Stream
        IStream* inputStream = NULL;
        HRESULT res = SHCreateStreamOnFileEx(
                    manifestPath,
                    STGM_READ | STGM_SHARE_EXCLUSIVE,
                    0, // default file attributes
                    FALSE, // do not create new file
                    NULL, // no template
                    &inputStream);
        if (!SUCCEEDED(res))
            return;

        // Appxfactory
        IAppxFactory* appxfac = NULL;
        GUID clsid;
        CLSIDFromString(L"{5842a140-ff9f-4166-8f5c-62f5b7b0c781}", &clsid);
        GUID iid;
        IIDFromString(L"{beb94909-e451-438b-b5a7-d79e767b75d8}", &iid);
        res = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void**)&appxfac);
        if (!SUCCEEDED(res))
            return;

        // Manifest reader
        IAppxManifestReader* reader = NULL;
        res = IAppxFactory_CreateManifestReader(appxfac, inputStream, (IAppxManifestReader**)&reader);
        if (!SUCCEEDED(res))
            return;

        // App enumerator
        IAppxManifestApplicationsEnumerator* appEnum = NULL;
        res = IAppxManifestReader_GetApplications(reader, &appEnum);
        if (!SUCCEEDED(res))
            return;

        IAppxManifestApplication* app = NULL;
        BOOL hasApp = false;
        IAppxManifestApplicationsEnumerator_GetHasCurrent(appEnum, &hasApp);
        IAppxManifestApplicationsEnumerator_GetCurrent(appEnum, &app);
        while (hasApp)
        {
            static wchar_t* aumid = NULL;
            IAppxManifestApplication_GetAppUserModelId(app, &aumid);
            if (!wcscmp(aumid, userModelID))
            {
                IAppxManifestApplication_GetStringValue(app, L"Square44x44Logo", &logoProp);
                break;
            }
            IAppxManifestApplicationsEnumerator_MoveNext(appEnum, &hasApp);
        }

        IAppxManifestApplicationsEnumerator_Release(appEnum);
        IAppxManifestReader_Release(reader);
        IAppxFactory_Release(appxfac);
        IStream_Release(inputStream);
        ASSERT(logoProp != NULL);
    }
    for (uint32_t i = 0; logoProp[i] != L'\0'; i++)
    {
        if (logoProp[i] == L'\\')
            logoProp[i] = L'/';
    }

    wchar_t logoPath[MAX_PATH];
    wcscpy(logoPath, packagePath);
    wcscat(logoPath, L"/");
    wcscat(logoPath, logoProp);

    wchar_t parentDir[MAX_PATH];
    wcscpy(parentDir, logoPath);
    *wcsrchr(parentDir, L'/') = L'\0';

    wchar_t parentDirStar[MAX_PATH];
    wcscpy(parentDirStar, parentDir);
    wcscat(parentDirStar, L"/*");

    wchar_t logoNoExt[MAX_PATH];
    wchar_t* atLastSlash = wcsrchr(logoProp, L'/');
    wcscpy(logoNoExt, atLastSlash ? atLastSlash + 1 : logoProp);
    wcsrchr(logoNoExt, L'.')[0] = L'\0';

    wchar_t ext[16];
    wcscpy(ext, wcsrchr(logoProp, L'.'));

    WIN32_FIND_DATAW findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    hFind = FindFirstFileW(parentDirStar, &findData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        CoUninitialize();
        return;
    }

    uint32_t maxSize = 0;
    bool foundAny = false;

    // https://learn.microsoft.com/en-us/windows/apps/design/style/iconography/app-icon-construction
    while (FindNextFileW(hFind, &findData) != 0)
    {
        const wchar_t* postLogoName = NULL;
        {
            const wchar_t* at = wcsstr(findData.cFileName, logoNoExt);
            if (at == NULL)
                continue;
            postLogoName = at + wcslen(logoNoExt);
        }

        uint32_t targetsize = 0;
        {
            const wchar_t* at = wcsstr(postLogoName, L"targetsize-");
            if (at != NULL)
            {
                at += (sizeof(L"targetsize-") / sizeof(wchar_t)) - 1;
                targetsize = wcstol(at, NULL, 10);
            }
        }

        const bool lightUnplated = wcsstr(postLogoName, L"altform-lightunplated") != NULL;
        const bool unplated = wcsstr(postLogoName, L"altform-unplated") != NULL;
        const bool constrast = wcsstr(postLogoName, L"contrast") != NULL;
        const bool matchingTheme = !constrast &&
            ((appData->_GraphicsResources._LightTheme && lightUnplated) ||
            (!appData->_GraphicsResources._LightTheme && unplated));

        if (targetsize > maxSize || !foundAny || (targetsize == maxSize && matchingTheme))
        {
            maxSize = targetsize;
            foundAny = true;
            wcscpy(outIconPath, parentDir);
            wcscat(outIconPath, L"/");
            wcscat(outIconPath, findData.cFileName);
        }
    }

    {
        wcscpy(iconMap->_Data[iconMap->_Head]._UserModelID, userModelID);
        wcscpy(iconMap->_Data[iconMap->_Head]._Icon, outIconPath);
        iconMap->_Count = min(iconMap->_Count + 1, UWPICONMAPSIZE);
        iconMap->_Head = Modulo(iconMap->_Head + 1, UWPICONMAPSIZE);
    }

    CoUninitialize();
}

#pragma pack(push)
#pragma pack(2)
typedef struct{
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    WORD nID;
} GRPICONDIRENTRY, *LPGRPICONDIRENTRY;

typedef struct {
    WORD idReserved;
    WORD idType;
    WORD idCount;
    GRPICONDIRENTRY idEntries[1];
} GRPICONDIR, *LPGRPICONDIR;
#pragma pack(pop)

static BOOL GetIconGroupName(HMODULE hModule, LPCSTR lpType, LPSTR lpName, LONG_PTR lParam)
{
    (void)hModule; (void)lpType; (void)lpName; (void)lParam;
    if (IS_INTRESOURCE(lpName))
    {
        *(char**)lParam = lpName;
    }
    else
    {
        strcpy(*(char**)lParam, lpName);
    }
    return false;
}

static GpBitmap* GetIconFromExe(const char* exePath)
{
    HMODULE module = LoadLibraryEx(exePath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ASSERT(module != NULL);

    // Finds icon resource in module
    uint32_t iconResID = 0xFFFFFFFF;
    uint32_t resByteSize = 0;
    {
        char name[256];
        char* pName = name;
        EnumResourceNames(module, RT_GROUP_ICON, GetIconGroupName, (LONG_PTR)&pName);
        HRSRC iconGrp = FindResource(module, pName, RT_GROUP_ICON);
        if (iconGrp == NULL)
        {
            FreeLibrary(module);
            return NULL;
        }
        HGLOBAL hGlobal = LoadResource(module, iconGrp);
        GRPICONDIR* iconGrpData = (GRPICONDIR*)LockResource(hGlobal);
        for (uint32_t i = 0; i < iconGrpData->idCount; i++)
        {
            const GRPICONDIRENTRY* entry = &iconGrpData->idEntries[i];
            if (entry->dwBytesInRes > resByteSize)
            {
                iconResID = entry->nID;
                resByteSize = entry->dwBytesInRes;
            }
        }
        UnlockResource(iconGrp);
        FreeResource(iconGrp);
    }

    // Loads a bitmap from icon resource (bitmap must be freed later)
    HBITMAP hbm = NULL;
    {
        HRSRC iconResInfo = FindResource(module, MAKEINTRESOURCE(iconResID), RT_ICON);
        HGLOBAL iconRes = LoadResource(module, iconResInfo);
        BYTE* data = (BYTE*)LockResource(iconRes);
        HICON icon = CreateIconFromResourceEx(data, resByteSize, true, 0x00030000, 0, 0, 0);
        UnlockResource(icon);
        FreeResource(iconRes);
        ICONINFO ii;
        GetIconInfo(icon, &ii);
        hbm = ii.hbmColor;
        DeleteObject(ii.hbmMask);
    }

    // Module not needed anymore
    FreeLibrary(module);
    module = NULL;

    // Creates a gdi bitmap from the win base api bitmap
    GpBitmap* out;
    {
        BITMAP bm;
        MEM_INIT(bm);
        GetObject(hbm, sizeof(BITMAP), &bm);
        const uint32_t iconSize = bm.bmWidth;
        GdipCreateBitmapFromScan0(iconSize, iconSize, 4 * iconSize, PixelFormat32bppARGB, NULL, &out);
        GpRect r = { 0, 0, iconSize, iconSize };
        BitmapData dstData;
        MEM_INIT(dstData);
        GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
        GetBitmapBits(hbm, sizeof(uint32_t) * iconSize * iconSize, dstData.Scan0);
        GdipBitmapUnlockBits(out, &dstData);
    }

    // Bitmap not needed anymore
    DeleteObject(hbm);
    hbm = NULL;

    return out;
}

static BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    if (!IsAltTabWindow(hwnd))
        return true;
    DWORD PID = 0;
    BOOL isUWP = false;

    FindActualPID(hwnd, &PID, &isUWP);
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);

    SAppData* appData = (SAppData*)lParam;
    SWinGroupArr* winAppGroupArr = &(appData->_WinGroups);

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
        ASSERT(group->_WindowCount == 0);
        // Icon
        const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, PID);
        if (process)
        {
            ASSERT(group->_IconBitmap == NULL);
            bool stdIcon = false;

            {
                HICON icon = ExtractIcon(process, group->_ModuleFileName, 0);
                stdIcon = icon != NULL;
                DestroyIcon(icon);
            }

            {
                static wchar_t userModelID[256];
                userModelID[0] = L'\0';
                uint32_t userModelIDLength = 256;
                GetApplicationUserModelId(process, &userModelIDLength, userModelID);
                isUWP = userModelID[0] != L'\0';
            }

            (void)stdIcon;
            if (!isUWP)
            {
                group->_IconBitmap = GetIconFromExe(group->_ModuleFileName);
            }
            else if (isUWP)
            {
                static wchar_t iconPath[MAX_PATH];
                iconPath[0] = L'\0';
                GetUWPIcon(process, iconPath, appData);
                GdipLoadImageFromFile(iconPath, &group->_IconBitmap);
            }

            if (group->_IconBitmap == NULL)
            {
                // Loads a bitmap from icon resource (bitmap must be freed later)
                HBITMAP hbm = NULL;
                {
                    HICON hi = NULL;
                    (void)hi;
                    LoadIconWithScaleDown(NULL, (PCWSTR)IDI_APPLICATION, 256, 256, &hi);
                    ICONINFO iconinfo;
                    GetIconInfo(hi, &iconinfo);
                    hbm = iconinfo.hbmColor;
                    DestroyIcon(hi);
                }

                // Creates a gdi bitmap from the win base api bitmap
                GpBitmap* out;
                {
                    BITMAP bm;
                    MEM_INIT(bm);
                    GetObject(hbm, sizeof(BITMAP), &bm);
                    const uint32_t iconSize = bm.bmWidth;
                    GdipCreateBitmapFromScan0(iconSize, iconSize, 4 * iconSize, PixelFormat32bppARGB, NULL, &out);
                    GpRect r = { 0, 0, iconSize, iconSize };
                    BitmapData dstData;
                    MEM_INIT(dstData);
                    GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
                    GetBitmapBits(hbm, sizeof(uint32_t) * iconSize * iconSize, dstData.Scan0);
                    GdipBitmapUnlockBits(out, &dstData);
                }

                DeleteObject(hbm);
                group->_IconBitmap = out; 
            }
        }
        CloseHandle(process);
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

static void ComputeMetrics(uint32_t iconCount, float scale, Metrics *metrics)
{
    const int centerY = GetSystemMetrics(SM_CYSCREEN) / 2;
    const int centerX = GetSystemMetrics(SM_CXSCREEN) / 2;
    const int screenWidth = GetSystemMetrics(SM_CXFULLSCREEN);
    const uint32_t iconContainerSize = min(scale * 2 * GetSystemMetrics(SM_CXICON), (screenWidth * 0.9) / iconCount);
    const uint32_t sizeX = iconCount * iconContainerSize;
    const uint32_t halfSizeX = sizeX / 2;
    const uint32_t sizeY = 1 * iconContainerSize;
    const uint32_t halfSizeY = sizeY / 2;
    metrics->_WinPosX = centerX - halfSizeX;
    metrics->_WinPosY = centerY - halfSizeY;
    metrics->_WinX = sizeX;
    metrics->_WinY = sizeY;
    metrics->_Icon = iconContainerSize / 2;
    metrics->_IconContainer = iconContainerSize;
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

static void DestroyWin(HWND win)
{
    DestroyWindow(win);
    win = NULL;
}

static void CreateWin(SAppData* appData)
{
    if (appData->_MainWin)
        DestroyWin(appData->_MainWin);

    ComputeMetrics(appData->_WinGroups._Size, appData->_Config._Scale, &appData->_Metrics);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, // Optional window styles (WS_EX_)
        CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP | WS_VISIBLE, // Window style
        // Pos and size
        appData->_Metrics._WinPosX, appData->_Metrics._WinPosY, appData->_Metrics._WinX, appData->_Metrics._WinY,
        NULL, // Parent window
        NULL, // Menu
        appData->_Instance, // Instance handle
        appData // Additional application data
    );

    ASSERT(hwnd);
    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    appData->_MainWin = hwnd;
}

static void InitializeSwitchApp(SAppData* appData)
{
    SWinGroupArr* pWinGroups = &(appData->_WinGroups);
    pWinGroups->_Size = 0;
    EnumDesktopWindows(NULL, FillWinGroups, (LPARAM)appData);
    appData->_Mode = ModeApp;
    appData->_Selection = 0;
    appData->_MouseSelection = 0;
    CreateWin(appData);
}

static void InitializeSwitchWin(SAppData* appData)
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
    SWinGroup* pWinGroup = &(appData->_CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->_ModuleFileName);
    pWinGroup->_WindowCount = 0;
    EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)pWinGroup);
    appData->_Selection = 0;
    appData->_Mode = ModeWin;
}
static void ClearWinGroupArr(SWinGroupArr* winGroups)
{
    for (uint32_t i = 0; i < winGroups->_Size; i++)
    {
        if (winGroups->_Data[i]._IconBitmap)
        {
            GdipDisposeImage(winGroups->_Data[i]._IconBitmap);
            winGroups->_Data[i]._IconBitmap = NULL;
        }
        winGroups->_Data[i]._WindowCount = 0;
    }
    winGroups->_Size = 0;
}

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
   // ASSERT(ForceSetForeground(winArr->_Data[0]));
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

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    const bool isAppHold = kbStrut.vkCode == _KeyConfig->_AppHold;
    const bool isAppSwitch = kbStrut.vkCode == _KeyConfig->_AppSwitch;
    const bool isPrevApp = kbStrut.vkCode == _KeyConfig->_PrevApp;
    const bool isWinHold = kbStrut.vkCode == _KeyConfig->_WinHold;
    const bool isWinSwitch = kbStrut.vkCode == _KeyConfig->_WinSwitch;
    const bool isInvert = kbStrut.vkCode == _KeyConfig->_Invert;
    const bool isTab = kbStrut.vkCode == VK_TAB;
    const bool isShift = kbStrut.vkCode == VK_LSHIFT;
    const bool isEscape = kbStrut.vkCode == VK_ESCAPE;
    const bool isWatchedKey = 
        isAppHold ||
        isAppSwitch ||
        isWinHold ||
        isWinSwitch ||
        isInvert ||
        isTab ||
        isShift ||
        (isPrevApp && _KeyConfig->_PrevApp != 0xFFFFFFFF) ||
        isEscape;

    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static KeyState keyState =  { false, false, false, false, false, false, false };
    static Mode mode = ModeNone;

    const KeyState prevKeyState = keyState;

    const bool releasing = kbStrut.flags & LLKHF_UP;

    // Update keyState
    {
        if (isAppHold)
            keyState._HoldAppDown = !releasing;
        if (isAppSwitch)
            keyState._SwitchAppDown = !releasing;
        if (isPrevApp)
            keyState._PrevAppDown = !releasing;
        if (isWinHold)
            keyState._HoldWinDown = !releasing;
        if (isWinSwitch)
            keyState._SwitchWinDown = !releasing;
        if (isInvert)
            keyState._InvertKeyDown = !releasing;
        if (isEscape)
            keyState._EscapeDown = !releasing;
    }

    // Update target app state
    bool bypassMsg = false;
    const Mode prevMode = mode;
    {
        const bool switchWinInput = !prevKeyState._SwitchWinDown && keyState._SwitchWinDown;
        const bool switchAppInput = !prevKeyState._SwitchAppDown && keyState._SwitchAppDown;
        const bool prevAppInput = !prevKeyState._PrevAppDown && keyState._PrevAppDown;
        const bool winHoldReleasing = prevKeyState._HoldWinDown && !keyState._HoldWinDown;
        const bool appHoldReleasing = prevKeyState._HoldAppDown && !keyState._HoldAppDown;
        const bool escapeInput = !prevKeyState._EscapeDown && keyState._EscapeDown;

        const bool switchApp =
            switchAppInput &&
            keyState._HoldAppDown;
        const bool prevApp =
            prevAppInput &&
            keyState._HoldAppDown;
        const bool switchWin =
            switchWinInput &&
            keyState._HoldWinDown;
        const bool cancel =
            escapeInput &&
            keyState._HoldAppDown;

        bool isApplying = false;

        // Denit.
        if ((prevMode == ModeApp) &&
            (switchWin || appHoldReleasing) && !prevApp)
        {
            mode = ModeNone;
            isApplying = true;
            PostThreadMessage(_MainThread, MSG_DEINIT_APP, 0, 0);
        }
        else if (prevMode == ModeWin &&
            (switchApp || winHoldReleasing))
        {
            mode = switchAppInput ? ModeApp : ModeNone;
            isApplying = true;
            PostThreadMessage(_MainThread, MSG_DEINIT_WIN, 0, 0);
        }
        else if (prevMode == ModeApp && cancel)
        {
            mode = ModeNone;
            isApplying = true;
            PostThreadMessage(_MainThread, MSG_CANCEL_APP, 0, 0);
        }

        if (mode == ModeNone && switchApp)
            mode = ModeApp;
        else if (mode == ModeNone && switchWin)
            mode = ModeWin;

        if (mode == ModeApp && prevMode != ModeApp)
        {
            PostThreadMessage(_MainThread, MSG_INIT_APP, 0, 0);
        }
        else if (mode == ModeWin && prevMode != ModeWin)
        {
            PostThreadMessage(_MainThread, MSG_INIT_WIN, 0, 0);
        }

        if (mode == ModeApp)
        {
            if (switchApp)
                PostThreadMessage(_MainThread, keyState._InvertKeyDown ? MSG_PREV_APP : MSG_NEXT_APP, 0, 0);
            else if (prevApp)
                PostThreadMessage(_MainThread, MSG_PREV_APP, 0, 0);
        }
        else if (switchWin)
        {
            PostThreadMessage(_MainThread, keyState._InvertKeyDown ? MSG_PREV_WIN : MSG_NEXT_WIN, 0, 0);
        }

        bypassMsg = 
            ((mode != ModeNone) || isApplying) &&
            (isWinSwitch || isAppSwitch || isWinHold || isAppHold || isInvert || isPrevApp);
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
            ASSERT(uSent == 3);
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

static bool TryGetFloat(const char* lineBuf, const char* token, float* floatToSet)
{
    const char* pValue = strstr(lineBuf, token);
    if (pValue != NULL)
    {
        *floatToSet = strtof(pValue + strlen(token)  - 1, NULL);
        return true;
    }
    return false;
}

static bool TryGetTheme(const char* lineBuf, const char* token, ThemeMode* theme)
{
    const char* pValue = strstr(lineBuf, token);
    if (pValue != NULL)
    {
        if (strstr(pValue + strlen(token) - 1, "auto") != NULL)
        {
            *theme = ThemeModeAuto;
            return true;
        }
        else if (strstr(pValue + strlen(token) - 1, "light") != NULL)
        {
            *theme = ThemeModeLight;
            return true;
        }
        else if (strstr(pValue + strlen(token) - 1, "dark") != NULL)
        {
            *theme = ThemeModeDark;
            return true;
        }
    }
    return false;
}
#include "_Generated/ConfigStr.h"
static void LoadConfig(Config* config)
{
    config->_Key._AppHold = VK_LMENU;
    config->_Key._AppSwitch = VK_TAB;
    config->_Key._WinHold = VK_LMENU;
    config->_Key._WinSwitch = VK_OEM_3;
    config->_Key._Invert = VK_LSHIFT;
    config->_Key._PrevApp = 0xFFFFFFFF;
    config->_Mouse = true;
    config->_ThemeMode = ThemeModeAuto;
    config->_Scale = 1.5;

    const char* configFile = "AltAppSwitcherConfig.txt";
    FILE* file = fopen(configFile ,"rb");
    if (file == NULL)
    {
        file = fopen(configFile ,"a");
        fprintf(file, ConfigStr);
        fclose(file);
        fopen(configFile ,"rb");
    }

    static char lineBuf[1024];
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        if (TryGetKey(lineBuf, "app hold key: ", &config->_Key._AppHold))
            continue;
        if (TryGetKey(lineBuf, "next app key: ", &config->_Key._AppSwitch))
            continue;
        if (TryGetKey(lineBuf, "previous app key: ", &config->_Key._PrevApp))
            continue;
        if (TryGetKey(lineBuf, "window hold key: ", &config->_Key._WinHold))
            continue;
        if (TryGetKey(lineBuf, "next window key: ", &config->_Key._WinSwitch))
            continue;
        if (TryGetKey(lineBuf, "invert order key: ", &config->_Key._Invert))
            continue;
        if (TryGetBool(lineBuf, "allow mouse: ", &config->_Mouse))
            continue;
        if (TryGetTheme(lineBuf, "theme: ", &config->_ThemeMode))
            continue;
        if (TryGetFloat(lineBuf, "scale: ", &config->_Scale))
            continue;
    }
    fclose(file);
}


LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static bool mouseInside = false;
    static SAppData* appData = NULL;
    switch (uMsg)
    {
    case WM_MOUSELEAVE:
    {
        mouseInside = false;
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (!appData->_Config._Mouse)
            return 0;
        if (!mouseInside)
        {
            // Mouse move event is fired on window show if the mouse is already in client area.
            // Skip first event so only actual move triggers mouse selection.
            mouseInside = true;
            return 0;
        }
        const int iconContainerSize = (int)appData->_Metrics._IconContainer;
        const int posX = GET_X_LPARAM(lParam);
        appData->_MouseSelection = min(max(0, posX / iconContainerSize), (int)appData->_WinGroups._Size);
        InvalidateRect(appData->_MainWin, 0, FALSE);
        UpdateWindow(appData->_MainWin);
        return 0;
    }
    case WM_CREATE:
    {
        appData = (SAppData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        {
            RECT clientRect;
            ASSERT(GetWindowRect(hwnd, &clientRect));

            HDC winDC = GetDC(hwnd);
            ASSERT(winDC);
            appData->_GraphicsResources._DC = CreateCompatibleDC(winDC);
            ASSERT(appData->_GraphicsResources._DC != NULL);
            appData->_GraphicsResources._Bitmap = CreateCompatibleBitmap(
                    winDC,
                    clientRect.right - clientRect.left,
                    clientRect.bottom - clientRect.top);
            ASSERT(appData->_GraphicsResources._Bitmap != NULL);
            ReleaseDC(hwnd, winDC);
        }

        mouseInside = false;
        SetCapture(hwnd);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!mouseInside)
        {
            appData->_Mode = ModeNone;
            DestroyWin(appData->_MainWin);
            ClearWinGroupArr(&appData->_WinGroups);
            return 0;
        }
        if (!appData->_Config._Mouse)
            return 0;
        const int selection = appData->_MouseSelection;
        appData->_Mode = ModeNone;
        appData->_Selection = 0;
        appData->_MouseSelection = 0;
        DestroyWin(appData->_MainWin);
        ApplySwitchApp(&appData->_WinGroups._Data[selection]);
        ClearWinGroupArr(&appData->_WinGroups);
        return 0;
    }
    case WM_DESTROY:
    {
        DeleteDC(appData->_GraphicsResources._DC);
        DeleteObject(appData->_GraphicsResources._Bitmap);
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        MEM_INIT(ps);
        if (BeginPaint(hwnd, &ps) == NULL)
        {
            ASSERT(false);
            return 0;
        }

        SGraphicsResources* pGraphRes = &appData->_GraphicsResources;

        HANDLE oldBitmap = SelectObject(pGraphRes->_DC, pGraphRes->_Bitmap);
        ASSERT(oldBitmap != NULL);
        ASSERT(oldBitmap != HGDI_ERROR);

        RECT clientRect;
        ASSERT(GetClientRect(hwnd, &clientRect));

        HBRUSH bgBrush = CreateSolidBrush(pGraphRes->_BackgroundColor);
        FillRect(pGraphRes->_DC, &clientRect, bgBrush);
        DeleteObject(bgBrush);

        SetBkMode(pGraphRes->_DC, TRANSPARENT); // ?

        GpGraphics* pGraphics = NULL;
        ASSERT(Ok == GdipCreateFromHDC(pGraphRes->_DC, &pGraphics));
        // gdiplus/gdiplusenums.h
        GdipSetSmoothingMode(pGraphics, 5);
        GdipSetPixelOffsetMode(pGraphics, 2);
        GdipSetInterpolationMode(pGraphics, 7); // InterpolationModeHighQualityBicubic

        const uint32_t iconSize = appData->_Metrics._Icon;
        const uint32_t iconContainerSize = 2.0 * iconSize;
        const uint32_t padding = (iconContainerSize - iconSize) / 2;
        uint32_t x = padding;
        for (uint32_t i = 0; i < appData->_WinGroups._Size; i++)
        {
            const SWinGroup* pWinGroup = &appData->_WinGroups._Data[i];

            if (i == (uint32_t)appData->_MouseSelection)
            {
                RECT rect = {x - padding / 2, padding / 2, x + iconSize + padding/2, padding + iconSize + padding/2 };
                DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushBgHighlight, rect.left, rect.top, rect.right, rect.bottom, 10);
            }

            if (i == (uint32_t)appData->_Selection)
            {
                COLORREF cr = pGraphRes->_TextColor;
                ARGB gdipColor = cr | 0xFF000000;
                GpPen* pPen;
                GdipCreatePen1(gdipColor, 3, 2, &pPen);
                RECT rect = {x - padding / 2, padding / 2, x + iconSize + padding/2, padding + iconSize + padding/2 };
                DrawRoundedRect(pGraphics, pPen, NULL, rect.left, rect.top, rect.right, rect.bottom, 10);
                GdipDeletePen(pPen);
            }

            // TODO: Check histogram and invert (or another filter) if background
            // is similar
            // https://learn.microsoft.com/en-us/windows/win32/api/gdiplusheaders/nf-gdiplusheaders-bitmap-gethistogram
            // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-remap-table-use
            // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-matrix-to-transform-a-single-color-use
            // Also check palette to see if monochrome
            if (pWinGroup->_IconBitmap)
            {
                GdipDrawImageRectI(pGraphics, pWinGroup->_IconBitmap, x, padding, iconSize, iconSize);
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
                // Invert text / bg brushes
                DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushText, rect[0], rect[1], rect[0] + rect[2], rect[1] + rect[3], 5);
                ASSERT(!GdipDrawString(pGraphics, count, digitsCount, pGraphRes->_pFont, &rectf, pGraphRes->_pFormat, pGraphRes->_pBrushBg));
            }
            x += iconContainerSize;
        }
        BitBlt(ps.hdc, clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, pGraphRes->_DC, 0, 0, SRCCOPY);

        // Always restore old bitmap (see fn doc)
        SelectObject(pGraphRes->_DC, oldBitmap);

        GdipDeleteGraphics(pGraphics);
        EndPaint(hwnd, &ps);

        return 0;
    }
    case WM_ERASEBKGND:
        return (LRESULT)0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static DWORD KbHookCb(LPVOID param)
{
    (void)param;
    ASSERT(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = { };

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {}

    return (DWORD)0;
}

int StartAltAppSwitcher(HINSTANCE hInstance)
{
    SetLastError(0);

    ULONG_PTR gdiplusToken = 0;
    {
        GdiplusStartupInput gdiplusStartupInput = {};
        gdiplusStartupInput.GdiplusVersion = 1;
        uint32_t status = GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        ASSERT(!status);
    }

    static SAppData _AppData;
    MEM_INIT(_AppData);

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc   = WindowProc;
        wc.hInstance     = hInstance;
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
        MEM_INIT(_AppData._WinGroups);
        // Hook needs globals
        _MainThread = GetCurrentThreadId();
        _KeyConfig = &_AppData._Config._Key;
        LoadConfig(&_AppData._Config);
        InitGraphicsResources(&_AppData._GraphicsResources, &_AppData._Config);
    }

    HANDLE threadKbHook = CreateRemoteThread(GetCurrentProcess(), NULL, 0, *KbHookCb, (void*)&_AppData, 0, NULL);
    (void)threadKbHook;

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
    ASSERT(AdjustTokenPrivileges(token, false, &priv, sizeof(priv), 0, 0));
    CloseHandle(token);

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        switch (msg.message)
        {
        case MSG_INIT_APP:
        {
            InitializeSwitchApp(&_AppData);
            break;
        }
        case MSG_INIT_WIN:
        {
            InitializeSwitchWin(&_AppData);
            break;
        }
        case MSG_NEXT_APP:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchApp(&_AppData);
            _AppData._Selection++;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            break;
        }
        case MSG_PREV_APP:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchApp(&_AppData);
            _AppData._Selection--;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            break;
        }
        case MSG_NEXT_WIN:
        {
            if (_AppData._Mode == ModeNone)
                InitializeSwitchWin(&_AppData);
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
                InitializeSwitchWin(&_AppData);
            _AppData._Selection--;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._CurrentWinGroup._WindowCount);

            HWND win = _AppData._CurrentWinGroup._Windows[_AppData._Selection];
            ApplySwitchWin(win);

            break;
        }
        case MSG_DEINIT_APP:
        {
            if (_AppData._Mode == ModeNone)
                break;
            const int selection = _AppData._Selection;
            _AppData._Mode = ModeNone;
            _AppData._Selection = 0;

            DestroyWin(_AppData._MainWin);
            ApplySwitchApp(&_AppData._WinGroups._Data[selection]);
            ClearWinGroupArr(&_AppData._WinGroups);
            break;
        }
        case MSG_CANCEL_APP:
        {
            _AppData._Mode = ModeNone;
            DestroyWin(_AppData._MainWin);
            ClearWinGroupArr(&_AppData._WinGroups);
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
    }

    GdiplusShutdown(gdiplusToken);
    UnregisterClass(CLASS_NAME, hInstance);
    return 0;
}