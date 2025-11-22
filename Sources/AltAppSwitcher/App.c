#define COBJMACROS
#define NTDDI_VERSION NTDDI_WIN10
#include <stdio.h>
#include <string.h>
#include <vsstyle.h>
#include <wchar.h>
#include <windef.h>
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <psapi.h>
#include <dwmapi.h>
#include <winnt.h>
#include <winscard.h>
#include <processthreadsapi.h>
#include <gdiplus.h>
#include <appmodel.h>
#include <shlwapi.h>
#include <winreg.h>
#include <windowsx.h>
#include <unistd.h>
// https://stackoverflow.com/questions/71437203/proper-way-of-activating-a-window-using-winapi
#include <Initguid.h>
#include <uiautomationclient.h>
#include <gdiplus/gdiplusenums.h>
#include <PropKey.h>
#include <winuser.h>
#include <winnt.h>
#include <pthread.h>
#include <time.h>
#include <Shobjidl.h>
#include "AppxPackaging.h"
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define MEM_INIT(ARG) memset(&ARG, 0,  sizeof(ARG))

#define ASYNC_APPLY

#define MAX_WIN_GROUPS 64u

typedef struct SWinGroup
{
    char _ModuleFileName[MAX_PATH];
    ATOM _WinClass;
    wchar_t _AppName[MAX_PATH];
    wchar_t _Caption[MAX_PATH];
    HWND _Windows[MAX_WIN_GROUPS];
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
    SWinGroup _Data[MAX_WIN_GROUPS];
    uint32_t _Size;
} SWinGroupArr;

typedef struct KeyState
{
    bool _InvertKeyDown;
    bool _HoldWinDown;
    bool _HoldAppDown;
} KeyState;

typedef struct SGraphicsResources
{
    GpSolidFill* _pBrushText;
    GpSolidFill* _pBrushBg;
    GpSolidFill* _pBrushBgHighlight;
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
    float _Container;
    float _Selection;
    float _Icon;
    float _Pad;
} Metrics;

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
    wchar_t _AppName[MAX_PATH];
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
    bool _Invert;
    int _Selection;
    int _MouseSelection;
    SGraphicsResources _GraphicsResources;
    SWinGroupArr _WinGroups;
    SWinGroup _CurrentWinGroup;
    SUWPIconMap _UWPIconMap;
    Config _Config;
    Metrics _Metrics;
    bool _Elevated;
    CRITICAL_SECTION _WorkerCS;
    HANDLE _WorkerWin;
    HMONITOR _MouseMonitor;
} SAppData;

typedef struct SFoundWin
{
    HWND _Data[64];
    uint32_t _Size;
} SFoundWin;

static const KeyConfig* _KeyConfig;
static DWORD _MainThread;

// Main thread
#define MSG_INVERT_PUSH (WM_USER + 1)
#define MSG_INVERT_REL (WM_USER + 2)
#define MSG_NEXT_WIN (WM_USER + 3)
#define MSG_NEXT_APP (WM_USER + 4)
#define MSG_PREV_APP (WM_USER + 6)
#define MSG_DEINIT (WM_USER + 7)
#define MSG_CANCEL_APP (WM_USER + 9)
#define MSG_RESTORE_KEY (WM_USER + 12)

// Apply thread
#define MSG_APPLY_APP (WM_USER + 1)
#define MSG_APPLY_WIN (WM_USER + 2)
#define MSG_APPLY_APP_MOUSE (WM_USER + 3)

// Main window
#define MSG_FOCUS (WM_USER + 1)

static void RestoreKey(WORD keyCode)
{
    // if (GetAsyncKeyState(VK_RCONTROL) & 0x8000)
    // {
    //     printf("WHY\n");
    // }
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = 0;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);

    {
        // Needed ?
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = _KeyConfig->_Invert;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
    {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000)
    {
        // printf("need reset key 0\n");
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetKeyState(VK_RCONTROL) & 0x8000)
    {
        // printf("need reset key 1\n");
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }
}

static void InitGraphicsResources(SGraphicsResources* pRes, const Config* config)
{
    // Text
    {
        GpStringFormat* pGenericFormat;
        ASSERT(Ok == GdipStringFormatGetGenericDefault(&pGenericFormat));
        ASSERT(Ok == GdipCloneStringFormat(pGenericFormat, &pRes->_pFormat));
        ASSERT(Ok == GdipSetStringFormatAlign(pRes->_pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipSetStringFormatLineAlign(pRes->_pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipSetStringFormatFlags(pRes->_pFormat, StringFormatFlagsNoClip | StringFormatFlagsDisplayFormatControl));
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
}

static const char* WindowsClassNamesToSkip[] =
{
    "Shell_TrayWnd",
    "DV2ControlHost",
    "MsgrIMEWindowClass",
    "SysShadow",
    "Button",
    "Windows.UI.Core.CoreWindow",
    "Dwm"
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
    if (b == 0)
        return 0;
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

static void FindActualPID(HWND hwnd, DWORD* PID)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    BOOL isUWP = false;
    {
        wchar_t UMI[512];
        GetWindowThreadProcessId(hwnd, PID);
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *PID);
        uint32_t size = 512;
        isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
        if (isUWP)
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
            isUWP = true;
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
        isUWP = true;
        return;
    }

    {
        GetWindowThreadProcessId(hwnd, PID);
        isUWP = false;
        return;
    }
}

bool BelongsToCurrentDesktop(HWND window)
{
    IVirtualDesktopManager* vdm = NULL; (void)vdm;
    CoInitialize(NULL);
    CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL, &IID_IVirtualDesktopManager, (void**)&vdm);

    WINBOOL isCurrent = true;
    if (vdm)
        IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(vdm, window, &isCurrent);

    IVirtualDesktopManager_Release(vdm);
    CoUninitialize();
    return isCurrent;
}

static bool IsWindowOnMonitor(HWND hwnd, HMONITOR targetMonitor)
{
    RECT windowRect;
    
    // For minimized windows, use the restored position instead of current position
    if (IsIconic(hwnd))
    {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp))
        {
            windowRect = wp.rcNormalPosition;
        }
        else
        {
            // Fallback to GetWindowRect if GetWindowPlacement fails
            if (!GetWindowRect(hwnd, &windowRect))
                return false;
        }
    }
    else
    {
        if (!GetWindowRect(hwnd, &windowRect))
            return false;
    }
    
    // Use MonitorFromRect for more accurate monitor detection
    // This considers the window's entire area, not just center point
    HMONITOR windowMonitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);
    
    // Use CompareObjectHandles if available (Windows 10+) for more robust comparison
    // Fall back to direct comparison for older Windows versions
    static HMODULE kernel32 = NULL;
    static BOOL (WINAPI *pCompareObjectHandles)(HANDLE, HANDLE) = NULL;
    static bool initialized = false;
    
    if (!initialized)
    {
        kernel32 = GetModuleHandleA("kernel32.dll");
        if (kernel32)
        {
            pCompareObjectHandles = (BOOL (WINAPI *)(HANDLE, HANDLE))
                GetProcAddress(kernel32, "CompareObjectHandles");
        }
        initialized = true;
    }
    
    if (pCompareObjectHandles)
    {
        return pCompareObjectHandles(windowMonitor, targetMonitor);
    }
    else
    {
        // Fallback to direct comparison for older Windows versions
        return windowMonitor == targetMonitor;
    }
}

static bool IsEligibleWindow(HWND hwnd, const SAppData* appData, bool ignoreMinimizedWindows)
{
    if (hwnd == GetShellWindow()) // Desktop
        return false;

    WINDOWINFO wi = {};
    wi.cbSize = sizeof(WINDOWINFO);
    GetWindowInfo(hwnd, &wi);
    if (!(wi.dwStyle & WS_VISIBLE))
        return false;
    // Chrome has sometime WS_EX_TOOLWINDOW while beeing an alttabable window
    if ((wi.dwExStyle & WS_EX_TOOLWINDOW) != 0)
         return false;
    if ((wi.dwExStyle & WS_EX_TOPMOST) != 0)
        return false;

    // Start at the root owner
    const HWND owner = GetWindow(hwnd, GW_OWNER); (void)owner;
    const HWND parent = GetAncestor(hwnd, GA_PARENT); (void)parent;
    const HWND dw = GetDesktopWindow(); (void)dw;
    // Taskbar window if: owner is self or WS_EX_APPWINDOW is set
    bool b = (wi.dwExStyle & WS_EX_APPWINDOW) != 0;(void)(b);
    bool isOwned = owner != hwnd && owner != NULL;
    if ((isOwned) && !(wi.dwExStyle & WS_EX_APPWINDOW))
        return false;

    if (!BelongsToCurrentDesktop(hwnd))
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

    // Filter apps by monitor if enabled
    if (appData->_Config._AppFilterMode == AppFilterModeMouseMonitor)
    {
        if (!IsWindowOnMonitor(hwnd, appData->_MouseMonitor))
            return true;
    }

    if (ignoreMinimizedWindows)
    {
        WINDOWPLACEMENT placement;
        placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &placement);
        if (placement.showCmd == SW_SHOWMINIMIZED)
            return false;
    }

    return true;
}

static void LoadIndirectString(const wchar_t* packagePath, const wchar_t* packageName, const wchar_t* resource, wchar_t* output)
{
    static wchar_t indirectStr[512];
    indirectStr[0] = L'\0';
    wcscat(indirectStr, L"@{");
    wcscat(indirectStr, packagePath);
    wcscat(indirectStr, L"\\resources.pri? ms-resource://");
    wcscat(indirectStr, packageName);
    wcscat(indirectStr, L"/");
    wcscat(indirectStr, resource);
    wcscat(indirectStr, L"}");
    if (S_OK == SHLoadIndirectString(indirectStr, output, 512, NULL))
        return;

    indirectStr[0] = L'\0';
    wcscat(indirectStr, L"@{");
    wcscat(indirectStr, packagePath);
    wcscat(indirectStr, L"\\resources.pri? ms-resource://");
    wcscat(indirectStr, packageName);
    wcscat(indirectStr, L"/resources/"); // Seems needed in some case.
    wcscat(indirectStr, resource);
    wcscat(indirectStr, L"}");
    SHLoadIndirectString(indirectStr, output, 512, NULL);
}

//https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/AppxPackingDescribeAppx/cpp/DescribeAppx.cpp
static void GetUWPIconAndAppName(HANDLE process, wchar_t* outIconPath, wchar_t* outAppName, SAppData* appData)
{
    static wchar_t userModelID[512];
    {
        uint32_t length = 512;
        GetApplicationUserModelId(process, &length, userModelID);
    }

    SUWPIconMap* iconMap = &appData->_UWPIconMap;
    for (uint32_t i = 0; i < iconMap->_Count; i++)
    {
        const uint32_t i0 = Modulo(iconMap->_Head - 1 - i, UWPICONMAPSIZE);
        if (wcscmp(iconMap->_Data[i0]._UserModelID, userModelID))
            continue;
        wcscpy(outIconPath, iconMap->_Data[i0]._Icon);
        wcscpy(outAppName, iconMap->_Data[i0]._AppName);
        return;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    PACKAGE_ID pid[32];
    uint32_t pidSize = sizeof(pid);
    GetPackageId(process, &pidSize, (BYTE*)pid);
    static wchar_t packagePath[MAX_PATH];
    uint32_t packagePathLength = MAX_PATH;
    GetPackagePath(pid, 0, &packagePathLength, packagePath);
    static wchar_t packageFullName[MAX_PATH];
    uint32_t packageFullNameLength = MAX_PATH;
    PackageFullNameFromId(pid, &packageFullNameLength, packageFullName);

    static wchar_t manifestPath[MAX_PATH];
    wcscpy(manifestPath, packagePath);
    wcscat(manifestPath, L"/AppXManifest.xml");
    // {
    //     wchar_t* src = pid[0].name; wchar_t* dst = outAppName;
    //     while (*src != L'\0' && *src != L'.' )
    //     {
    //         *dst = *src; dst++; src++;
    //     }
    //     *dst = L'\0';
    // }

    wchar_t* logoProp = NULL;
    wchar_t* displayName = NULL;
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


        IAppxFactory* appxfac = NULL;
        // Appxfactory:
        // CLSID_AppxFactory and IID_IAppxFactory are declared as extern in "AppxPackaging.h"
        // I don't know where the symbols are defined, thus the hardcoded GUIDs here.
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
        while (hasApp)
        {
            static wchar_t* aumid = NULL;
            IAppxManifestApplicationsEnumerator_GetCurrent(appEnum, &app);
            IAppxManifestApplication_GetAppUserModelId(app, &aumid);
            if (!wcscmp(aumid, userModelID))
            {
                IAppxManifestApplication_GetStringValue(app, L"Square44x44Logo", &logoProp);
                IAppxManifestApplication_GetStringValue(app, L"DisplayName", &displayName);
                break;
            }
            IAppxManifestApplicationsEnumerator_MoveNext(appEnum, &hasApp);
        }

        IAppxManifestApplicationsEnumerator_Release(appEnum);
        IAppxManifestReader_Release(reader);
        IAppxFactory_Release(appxfac);
        IStream_Release(inputStream);
        VERIFY(logoProp != NULL);
        VERIFY(displayName != NULL);
        if (logoProp == NULL || displayName == NULL)
        {
            CoUninitialize();
            return;
        }
    }
    for (uint32_t i = 0; logoProp[i] != L'\0'; i++)
    {
        if (logoProp[i] == L'\\')
            logoProp[i] = L'/';
    }

    {
        if (wcsstr(displayName, L"ms-resource:") == displayName)
        {
            LoadIndirectString(packagePath, pid[0].name, &displayName[12], outAppName);
        }
        else
            wcscpy(outAppName, displayName);
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
        wcscpy(iconMap->_Data[iconMap->_Head]._AppName, outAppName);
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

static void GetAppName(const wchar_t* exePath, wchar_t* out)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IShellItem2* shellItem = NULL;
    {
        // DWORD res = CoCreateInstance(&CLSID_ShellItem, NULL, CLSCTX_INPROC_SERVER, &IID_IShellItem2, (void**)&IShellItem2);
        // ASSERT(SUCCEEDED(res))
    }
    DWORD res = SHCreateItemFromParsingName(exePath, NULL, &IID_IShellItem2, (void**)&shellItem);
    ASSERT(SUCCEEDED(res))
    wchar_t* siStr = NULL;
    res = IShellItem2_GetString(shellItem, &PKEY_FileDescription, &siStr);
    if (SUCCEEDED(res))
    {
        wcscpy(out, siStr);
        IShellItem2_Release(shellItem);
        CoUninitialize();
        return;
    }

    // Fallback to filename
    static wchar_t temp[MAX_PATH];
    wcscpy(temp, exePath);
    WStrBToF(temp);
    wchar_t* lastSlash = NULL;
    // wchar_t* lastDot = NULL;
    for (wchar_t* p = temp; *p != L'\0'; p++)
    {
        if (*p == L'/') lastSlash = p;
    }
    if (lastSlash == NULL)
        return;
    wcscpy(out, lastSlash + 1);
}

static GpBitmap* GetIconFromExe(const char* exePath)
{
    HMODULE module = LoadLibraryEx(exePath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ASSERT(module != NULL);

    // Finds icon resource in module
    uint32_t iconResID = 0xFFFFFFFF;
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
        uint32_t resByteSize = 0;
        for (uint32_t i = 0; i < iconGrpData->idCount; i++)
        {
            const GRPICONDIRENTRY* entry = &iconGrpData->idEntries[i];
            if (entry->dwBytesInRes > resByteSize)
            {
                iconResID = entry->nID;
                resByteSize = entry->dwBytesInRes;
            }
        }
        UnlockResource(hGlobal);
        FreeResource(iconGrp);
    }
    
    // Loads a bitmap from icon resource (bitmap must be freed later)
    HBITMAP hbm = NULL;
    HBITMAP hbmMask = NULL;
    {
        HRSRC iconResInfo = FindResource(module, MAKEINTRESOURCE(iconResID), RT_ICON);
        ASSERT(iconResInfo);
        HGLOBAL iconRes = LoadResource(module, iconResInfo);
        ASSERT(iconRes);
        BYTE* data = (BYTE*)LockResource(iconRes);
        const DWORD resByteSize = SizeofResource(module, iconResInfo);
        HICON icon = CreateIconFromResourceEx(data, resByteSize, true, 0x00030000, 0, 0, LR_DEFAULTCOLOR);
        ASSERT(icon);
        UnlockResource(iconRes);
        FreeResource(iconRes);
        ICONINFO ii;
        GetIconInfo(icon, &ii);
        hbm = ii.hbmColor;
        hbmMask= ii.hbmMask;
        DestroyIcon(icon);
    }

    // Module not needed anymore
    FreeLibrary(module);
    module = NULL;

    // Creates a gdi bitmap from the win base api bitmap
    GpBitmap* out;
    {
        BITMAP bm = {};
        GetObject(hbm, sizeof(BITMAP), &bm);
        const uint32_t iconSize = bm.bmWidth;
        GdipCreateBitmapFromScan0(iconSize, iconSize, 4 * iconSize, PixelFormat32bppARGB, NULL, &out);
        GpRect r = { 0, 0, iconSize, iconSize };
        BitmapData dstData = {};
        GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
        GetBitmapBits(hbm, sizeof(uint32_t) * iconSize * iconSize, dstData.Scan0);
        // Check if color has non zero alpha (is there an alternative)
        unsigned int* ptr = (unsigned int*)dstData.Scan0;
        bool noAlpha = true;
        for (int i = 0; i < iconSize * iconSize; i++)
        {
            if (ptr[i] & 0xFF000000)
            {
                noAlpha = false;
                break;
            }
        }
        // If no alpha, init
        if (noAlpha && hbmMask != NULL && iconSize <= 256)
        {
            BITMAP bitmapMask = {};
            GetObject(hbmMask, sizeof(bitmapMask), (LPVOID)&bitmapMask);
            unsigned int maskByteSize = bitmapMask.bmWidthBytes * bitmapMask.bmHeight;
            static char maskData[256 * 256 * 1 / 8];
            memset(maskData, 0, maskByteSize);
            GetBitmapBits(hbmMask, maskByteSize, maskData);
            for (int i = 0; i < iconSize * iconSize; i++)
            {
                unsigned int aFromMask = (0x1 & (maskData[i / 8] >> (7 - i % 8))) ? 0 : 0xFF000000;
                ptr[i] = ptr[i] | aFromMask;
            }
        }
        GdipBitmapUnlockBits(out, &dstData);
    }

    // Bitmap not needed anymore
    DeleteObject(hbm);
    DeleteObject(hbmMask);
    hbm = NULL;

    return out;
}
static BOOL IsRunWindow(HWND hwnd)
{
    {
        WINDOWINFO wi = {};
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(hwnd, &wi);
        if (wi.atomWindowType != 0x8002)
            return false;
    }

    const HWND owner = GetAncestor(hwnd, GA_ROOTOWNER);
    if (owner == NULL)
        return false;

    {
        WINDOWINFO wi = {};
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(owner, &wi);
        if (wi.atomWindowType != 0xC01A)
            return false;
    }

    return true;
}

static BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    SAppData* appData = (SAppData*)lParam;

    if (!IsEligibleWindow(hwnd, appData, false))
        return true;

    DWORD PID = 0;

    FindActualPID(hwnd, &PID);
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);

    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run

#if 0
    HICON classIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    (void)classIcon;
#endif
    // LONG_PTR winProc = GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    // static char winProcStr[] = "FFFFFFFFFFFFFFFF";
    // sprintf(winProcStr, "%08lX", (unsigned long)winProc);
    // strcat(moduleFileName, winProcStr);

    SWinGroupArr* winAppGroupArr = &(appData->_WinGroups);

    if (appData->_Config._AppSwitcherMode == AppSwitcherModeApp)
    {
        for (uint32_t i = 0; i < winAppGroupArr->_Size; i++)
        {
            SWinGroup* const group = &(winAppGroupArr->_Data[i]);
            if (group->_WinClass == winClass && !strcmp(group->_ModuleFileName, moduleFileName))
            {
                // Group found
                static wchar_t caption[MAX_PATH];
                caption[0] = L'\0';
                GetWindowTextW(hwnd, caption, MAX_PATH);
                if (wcscmp(caption, group->_Caption))
                {
                    // If caption differs, set group caption to null string
                    group->_Caption[0] = L'\0';
                }
                group->_Windows[group->_WindowCount++] = hwnd;
                return true;
            }
        }
    }

    // No group found
    SWinGroup* group = NULL;
    {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        if (!process)
        {
            CloseHandle(process);
            return true;
        }
        HANDLE tok;
        OpenProcessToken(process, TOKEN_QUERY, &tok);
        TOKEN_ELEVATION elTok;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
        bool elevated = elTok.TokenIsElevated;
        CloseHandle(tok);

        if (elevated && !appData->_Elevated)
            return true;

        group = &winAppGroupArr->_Data[winAppGroupArr->_Size++];
        strcpy(group->_ModuleFileName, moduleFileName);
        group->_WinClass = winClass;
        ASSERT(group->_WindowCount == 0);

        // Icon
        ASSERT(group->_IconBitmap == NULL);

#if 0
        bool stdIcon = false;
        {
            HICON icon = ExtractIcon(process, group->_ModuleFileName, 0);
            stdIcon = icon != NULL;
            DestroyIcon(icon);
        }
        (void)stdIcon;
#endif

        BOOL isUWP = false;
        {
            static wchar_t userModelID[256];
            userModelID[0] = L'\0';
            uint32_t userModelIDLength = 256;
            GetApplicationUserModelId(process, &userModelIDLength, userModelID);
            isUWP = userModelID[0] != L'\0';
        }

        if (!isUWP)
        {
            group->_IconBitmap = GetIconFromExe(group->_ModuleFileName);
            group->_AppName[0] = L'\0';
            static wchar_t exePath[MAX_PATH];
            mbstowcs(exePath, group->_ModuleFileName, MAX_PATH);
            GetAppName(exePath, group->_AppName);
        }
        else if (isUWP)
        {
            static wchar_t iconPath[MAX_PATH];
            iconPath[0] = L'\0';
            group->_AppName[0] = L'\0';
            GetUWPIconAndAppName(process, iconPath, group->_AppName, appData);
            GdipLoadImageFromFile(iconPath, &group->_IconBitmap);
        }

        {
            group->_Caption[0] = L'\0';
            GetWindowTextW(hwnd, group->_Caption, MAX_PATH);
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
        CloseHandle(process);
    }

    group->_Windows[group->_WindowCount++] = hwnd;
    return true;
}

static BOOL FillCurrentWinGroup(HWND hwnd, LPARAM lParam)
{
    SAppData* appData = (SAppData*)(lParam);
    if (!IsEligibleWindow(hwnd, appData, !appData->_Config._RestoreMinimizedWindows))
        return true;
    DWORD PID = 0;
    FindActualPID(hwnd, &PID);
    SWinGroup* currentWinGroup = &appData->_CurrentWinGroup;
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run
    if (strcmp(moduleFileName, currentWinGroup->_ModuleFileName) || currentWinGroup->_WinClass != winClass)
        return true;
    currentWinGroup->_Windows[currentWinGroup->_WindowCount] = hwnd;
    currentWinGroup->_WindowCount++;
    return true;
}

static void ComputeMetrics(uint32_t iconCount, float scale, Metrics *metrics, bool monitorModeMouse)
{
    uint32_t monitorOffset[2] = { 0, 0 };
    uint32_t monitorSize[2] = { 0, 0 };

    POINT mousePos = { 0, 0 };
    if (monitorModeMouse)
        GetCursorPos(&mousePos);
    HMONITOR monitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &info);
    monitorOffset[0] = info.rcMonitor.left;
    monitorOffset[1] = info.rcMonitor.top;
    monitorSize[0] = info.rcMonitor.right - info.rcMonitor.left;
    monitorSize[1] = info.rcMonitor.bottom - info.rcMonitor.top;

    scale = max(scale, 0.5f);
    const int centerY = monitorSize[1] / 2;
    const int centerX = monitorSize[0] / 2;
    const int screenWidth = monitorSize[0];
    const float containerRatio = 1.25f;
    const float selectRatio = 1.25f; // Same as container, legacy was different
    float iconSize = GetSystemMetrics(SM_CXICON) * scale;
    const float padRatio = max(0.25 * iconSize, 16.0f) / iconSize; // Keep room for app name
    const uint32_t sizeX = min(iconSize * (iconCount * containerRatio + 2.0f * padRatio), screenWidth * 0.9);
    iconSize = sizeX / (iconCount * containerRatio + 2.0f * padRatio);
    const uint32_t halfSizeX = sizeX / 2;
    const uint32_t sizeY = 1 * iconSize * containerRatio + 2.0f * padRatio * iconSize;
    const uint32_t halfSizeY = sizeY / 2;
    metrics->_WinPosX = centerX - halfSizeX + monitorOffset[0];
    metrics->_WinPosY = centerY - halfSizeY + monitorOffset[1];
    metrics->_WinX = sizeX;
    metrics->_WinY = sizeY;
    metrics->_Icon = iconSize;
    metrics->_Container = iconSize * containerRatio;
    metrics->_Selection =  iconSize * selectRatio;
    metrics->_Pad = iconSize * padRatio; 
}

static const char MAIN_CLASS_NAME[] = "AltAppSwitcher";
static const char WORKER_CLASS_NAME[] = "AASWorker";
static const char FOCUS_CLASS_NAME[] = "AASFocus";

static void DestroyWin(HWND* win)
{
    DestroyWindow(*win);
    *win = NULL;
}

static void Draw(SAppData* appData, HDC dc, RECT clientRect);
static void UIASetFocus(HWND win);

static void CreateWin(SAppData* appData)
{
    if (appData->_MainWin)
        DestroyWin(&appData->_MainWin);

    if (appData->_WinGroups._Size == 0)
        return;

    ComputeMetrics(appData->_WinGroups._Size, appData->_Config._Scale, &appData->_Metrics, appData->_Config._MultipleMonitorMode == MultipleMonitorModeMouse);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, // Optional window styles (WS_EX_)
        MAIN_CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP, // Window style
        // Pos and size
        appData->_Metrics._WinPosX, appData->_Metrics._WinPosY, appData->_Metrics._WinX, appData->_Metrics._WinY,
        NULL, // Parent window
        NULL, // Menu
        appData->_Instance, // Instance handle
        appData // Additional application data
    );
    ASSERT(hwnd);

    // Needed for exact client area.
    RECT r = { appData->_Metrics._WinPosX,
        appData->_Metrics._WinPosY, 
        appData->_Metrics._WinPosX + appData->_Metrics._WinX,
        appData->_Metrics._WinPosY + appData->_Metrics._WinY };
    AdjustWindowRect(&r, (DWORD)GetWindowLong(hwnd, GWL_STYLE), false);
    SetWindowPos(hwnd, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);

    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateWindow(hwnd);
    const DWORD ret = SetForegroundWindow(hwnd);
    (void)ret;
    // ASSERT(ret != 0);
    appData->_MainWin = hwnd;

    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    RECT clientRect = { 0, 0, appData->_Metrics._WinX, appData->_Metrics._WinY };
    Draw(appData, GetDC(hwnd), clientRect);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    //AnimateWindow(hwnd, 1, AW_ACTIVATE | AW_BLEND);
}
static void ClearWinGroupArr(SWinGroupArr* winGroups);

static void InitializeSwitchApp(SAppData* appData)
{
    SWinGroupArr* pWinGroups = &(appData->_WinGroups);
    pWinGroups->_Size = 0;
    
    // Get mouse monitor once if filtering by monitor is enabled
    if (appData->_Config._AppFilterMode == AppFilterModeMouseMonitor)
    {
        POINT mousePos;
        if (GetCursorPos(&mousePos))
        {
            appData->_MouseMonitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTONEAREST);
        }
        else
        {
            // Fall back to primary monitor if GetCursorPos fails
            appData->_MouseMonitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
        }
    }
    else
    {
        appData->_MouseMonitor = NULL; // Explicitly set NULL when not filtering by monitor
    }
    
    EnumDesktopWindows(NULL, FillWinGroups, (LPARAM)appData);
    appData->_Mode = ModeApp;
    appData->_Selection = 0;
    appData->_MouseSelection = 0;
    CreateWin(appData);
}

static void InitializeSwitchWin(SAppData* appData)
{
    HWND win = GetForegroundWindow();
    while (true)
    {
        if (!win || IsEligibleWindow(win, appData, false))
            break;
        win = GetParent(win);
    }
    if (!win)
        return;
    DWORD PID;
    FindActualPID(win, &PID);
    SWinGroup* pWinGroup = &(appData->_CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->_ModuleFileName);
    pWinGroup->_WinClass = IsRunWindow(win) ? 0x8002 : 0; // Run
    pWinGroup->_WindowCount = 0;
    if (appData->_Config._AppSwitcherMode == AppSwitcherModeApp)
        EnumDesktopWindows(NULL, FillCurrentWinGroup, (LPARAM)appData);
    else
    {
        pWinGroup->_Windows[0] = win;
        pWinGroup->_WindowCount = 1;
    }
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
        winGroups->_Data[i]._AppName[0] = L'\0';
        winGroups->_Data[i]._Caption[0] = L'\0';
    }
    winGroups->_Size = 0;
}

static void RestoreWin(HWND win)
{
    if (!IsWindow(win))
        return;
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED)
    {
        ShowWindowAsync(win, SW_RESTORE);
        SetWindowPos(win, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_ASYNCWINDOWPOS); // Why this call?
    }
}

static void UIASetFocus(HWND win)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IUIAutomation* UIA = NULL;
    DWORD res = CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void**)&UIA);
    ASSERT(SUCCEEDED(res))

    IUIAutomationElement* el = NULL;
    res = IUIAutomation_ElementFromHandle(UIA, win, &el);
    VERIFY(SUCCEEDED(res));
    res = IUIAutomationElement_SetFocus(el);
    VERIFY(SUCCEEDED(res));
    IUIAutomationElement_Release(el);

    IUIAutomation_Release(UIA);
    CoUninitialize();
}

typedef struct ApplySwitchAppData
{
    HWND _Data[64];
    unsigned int _Count;
    DWORD _fgWinThread;
} ApplySwitchAppData;


static void ApplySwitchApp(const SWinGroup* winGroup, bool restoreMinimized)
{
    // Set focus for all win, not only the last one. This way when the active window is closed,
    // the second to last window of the group becomes the active one.
    HWND fgWin = GetForegroundWindow();
    DWORD curThread = GetCurrentThreadId();
    DWORD fgWinThread = GetWindowThreadProcessId(fgWin, NULL);
    (void)curThread; (void)fgWinThread;
    DWORD ret; (void)ret;

    int winCount = (int)winGroup->_WindowCount;

    if (restoreMinimized)
    {
        for (int i = winCount - 1; i >= 0 ; i--)
        {
            const HWND win = winGroup->_Windows[Modulo(i + 1, winCount)];
            RestoreWin(win);
        }
    }

    HWND prev = HWND_TOP;
    HDWP dwp = BeginDeferWindowPos(winGroup->_WindowCount);
    ASSERT(dwp != 0);
    for (int i = winCount - 1; i >= 0 ; i--)
    {
        const HWND win = winGroup->_Windows[Modulo(i + 1, winCount)];
        if (!IsWindow(win))
            continue;
#if 1
        UIASetFocus(win);
#endif

        // This seems more consistent than SetFocus
        // Check if this works with focus when closing multiple win
        DWORD targetWinThread = GetWindowThreadProcessId(win, NULL);
        (void)targetWinThread;
        ret = AttachThreadInput(targetWinThread, curThread, TRUE);
        // ASSERT(ret != 0);

        dwp = DeferWindowPos(dwp, win, prev, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        // ASSERT(dwp != 0);
        prev = win;
    }

    ret = EndDeferWindowPos(dwp);
    // ASSERT(ret != 0);

    for (int i = winCount - 1; i >= 0 ; i--)
    {
        const HWND win = winGroup->_Windows[Modulo(i + 1, winCount)];
        if (!IsWindow(win))
            continue;
        DWORD targetWinThread = GetWindowThreadProcessId(win, NULL);
        (void)targetWinThread;
        ret = AttachThreadInput(targetWinThread, curThread, FALSE);
        // ASSERT(ret != 0);
    }
}

#ifdef ASYNC_APPLY
static DWORD WorkerThread(LPVOID data)
{
    SAppData* appData = (SAppData*)data;

    HANDLE window = CreateWindowEx(WS_EX_TOPMOST, WORKER_CLASS_NAME, NULL, WS_POPUP,
        0, 0, 0, 0, HWND_MESSAGE, NULL, appData->_Instance,appData);
    (void)window;

    EnterCriticalSection(&appData->_WorkerCS);
    appData->_WorkerWin = window;
    LeaveCriticalSection(&appData->_WorkerCS);
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnterCriticalSection(&appData->_WorkerCS);
    appData->_WorkerWin = NULL;
    LeaveCriticalSection(&appData->_WorkerCS);
    return 0;
}

static void ApplyWithTimeout(SAppData* appData, unsigned int msg)
{
    EnterCriticalSection(&appData->_WorkerCS);
    appData->_WorkerWin = NULL;
    LeaveCriticalSection(&appData->_WorkerCS);

    DWORD tid;
    HANDLE ht = CreateThread(NULL, 0, WorkerThread, (void*)appData, 0, &tid);
    ASSERT(ht != NULL);

    while (true)
    {
        if (TryEnterCriticalSection(&appData->_WorkerCS))
        {
            const bool initialized = appData->_WorkerWin != NULL;
            LeaveCriticalSection(&appData->_WorkerCS);
            if (initialized)
                break;
        }
        usleep(100);
    }

    HWND fgWin = GetForegroundWindow();
    DWORD fgWinThread = GetWindowThreadProcessId(fgWin, NULL);
    (void)fgWinThread;
    DWORD ret = 0; (void)ret;

    ret = SetForegroundWindow(appData->_WorkerWin);
    // ASSERT(ret != 0);

    SendNotifyMessage(appData->_WorkerWin, msg, 0, 0);

    time_t start;
    time(&start);
    while (true)
    {
        if (TryEnterCriticalSection(&appData->_WorkerCS))
        {
            const bool done = appData->_WorkerWin == NULL;
            LeaveCriticalSection(&appData->_WorkerCS);
            if (done)
                break;
        }
        time_t now;
        time(&now);
        const double dt = difftime(now, start);
        if (dt > 1.0)
            break;
    }

    CloseHandle(ht);
}
#endif

static void ApplySwitchWin(HWND win, bool restoreMinimized)
{
    if (restoreMinimized)
        RestoreWin(win);
    UIASetFocus(win);
}

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.vkCode == _KeyConfig->_AppHold;
    const bool nextAppKey = kbStrut.vkCode == _KeyConfig->_AppSwitch;
    const bool prevAppKey = kbStrut.vkCode == _KeyConfig->_PrevApp;
    const bool winHoldKey = kbStrut.vkCode == _KeyConfig->_WinHold;
    const bool nextWinKey = kbStrut.vkCode == _KeyConfig->_WinSwitch;
    const bool invertKey = kbStrut.vkCode == _KeyConfig->_Invert;
    const bool tabKey = kbStrut.vkCode == VK_TAB;
    const bool shiftKey = kbStrut.vkCode == VK_LSHIFT;
    const bool escKey = kbStrut.vkCode == VK_ESCAPE;
    const bool isWatchedKey =
        appHoldKey ||
        nextAppKey ||
        prevAppKey ||
        winHoldKey ||
        nextWinKey ||
        invertKey ||
        tabKey ||
        shiftKey ||
        escKey;
    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static Mode mode = ModeNone;

    const bool rel = kbStrut.flags & LLKHF_UP;

    // Update target app state
    bool bypassMsg = false;
    const Mode prevMode = mode;
    {
        const bool prevAppInput = prevAppKey && !rel;
        const bool escapeInput = escKey && !rel;
        const bool winHoldRelease = winHoldKey && rel;
        const bool appHoldRelease = appHoldKey && rel;
        const bool invertPush = invertKey && !rel;
        const bool invertRelease = invertKey && rel;
        const bool nextApp = nextAppKey && !rel;
        const bool prevApp = prevAppInput && !rel;
        const bool nextWin = nextWinKey && !rel;
        const bool cancel = escapeInput;
        const bool isWinHold = GetAsyncKeyState(_KeyConfig->_WinHold) & 0x8000;
        const bool isAppHold = GetAsyncKeyState(_KeyConfig->_AppHold) & 0x8000;

        // Denit.
        if (prevMode == ModeApp &&
            appHoldRelease)
        {
            mode = ModeNone;
            PostThreadMessage(_MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(_MainThread, MSG_RESTORE_KEY, _KeyConfig->_AppHold, 0);
            bypassMsg = true;
        }
        else if (prevMode == ModeWin && 
            winHoldRelease)
        {
            mode = ModeNone;
            PostThreadMessage(_MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(_MainThread, MSG_RESTORE_KEY, _KeyConfig->_WinHold, 0);
            bypassMsg = true;
        }
        else if (prevMode == ModeApp && cancel)
        {
            mode = ModeNone;
            PostThreadMessage(_MainThread, MSG_CANCEL_APP, 0, 0);
            PostThreadMessage(_MainThread, MSG_RESTORE_KEY, _KeyConfig->_AppHold, 0);
            bypassMsg = true;
        }

        if (nextApp && isAppHold)
        {
            mode = ModeApp;
            PostThreadMessage(_MainThread, MSG_NEXT_APP, 0, 0);
            bypassMsg = true;
        }
        else if (nextWin && isWinHold)
        {
            mode = ModeWin;
            PostThreadMessage(_MainThread, MSG_NEXT_WIN, 0, 0);
            bypassMsg = true;
        }
        else if (invertPush)
        {
            PostThreadMessage(_MainThread, MSG_INVERT_PUSH, 0, 0);
        }
        else if (invertRelease)
        {
            PostThreadMessage(_MainThread, MSG_INVERT_REL, 0, 0);
        }

        if (prevApp && isAppHold) // Not *else* if because the key can be shared
        {
            mode = ModeApp;
            PostThreadMessage(_MainThread, MSG_PREV_APP, 0, 0);
            bypassMsg = true;
        }
    }

    if (bypassMsg)
        return 1;

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void DrawRoundedRect(GpGraphics* pGraphics, GpPen* pPen, GpBrush* pBrush, const RectF* re, float di)
{
    float l = (re->X);
    float t = (re->Y);
    float b = (t + re->Height);
    float r = (l + re->Width);
    GpPath* pPath;
    GdipCreatePath(FillModeAlternate, &pPath);
    GdipAddPathArcI(pPath, l, t, di, di, 180, 90);
    GdipAddPathArcI(pPath, r - di, t, di, di, 270, 90);
    GdipAddPathArcI(pPath, r - di, b - di, di, di, 360, 90);
    GdipAddPathArcI(pPath, l, b - di, di, di, 90, 90);
    GdipAddPathLineI(pPath, l, b - di * 0.5, l, t + di * 0.5);
    GdipClosePathFigure(pPath);
    if (pBrush)
        GdipFillPath(pGraphics, pBrush, pPath);
    if (pPen)
        GdipDrawPath(pGraphics, pPen, pPath);
    GdipDeletePath(pPath);
}

static bool IsInside(int x, int y, HWND win)
{
    RECT r = {};
    ASSERT(GetClientRect(win, &r));
    return x > r.left && x < r.right && y > r.top && y < r.bottom;
}

static void Draw(SAppData* appData, HDC dc, RECT clientRect)
{
    HWND hwnd = appData->_MainWin;
    SGraphicsResources* pGraphRes = &appData->_GraphicsResources;

    HANDLE oldBitmap = SelectObject(pGraphRes->_DC, pGraphRes->_Bitmap);
    ASSERT(oldBitmap != NULL);
    ASSERT(oldBitmap != HGDI_ERROR);

    HBRUSH bgBrush = CreateSolidBrush(pGraphRes->_BackgroundColor);
    FillRect(pGraphRes->_DC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(pGraphRes->_DC, TRANSPARENT); // ?

    GpGraphics* pGraphics = NULL;
    ASSERT(Ok == GdipCreateFromHDC(pGraphRes->_DC, &pGraphics));
    // gdiplus/gdiplusenums.h
    GdipSetSmoothingMode(pGraphics, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(pGraphics, PixelOffsetModeHighQuality);
    GdipSetInterpolationMode(pGraphics, InterpolationModeHighQualityBilinear); // InterpolationModeHighQualityBicubic
    GdipSetTextRenderingHint(pGraphics, TextRenderingHintClearTypeGridFit);

    const float containerSize = appData->_Metrics._Container;
    const float iconSize = appData->_Metrics._Icon;
    const float selectSize = appData->_Metrics._Selection;
    const float pad = appData->_Metrics._Pad;
    const float padSelect = (containerSize - selectSize) * 0.5f;
    const float padIcon = (containerSize - iconSize) * 0.5f;
    const float digitBoxHeight = min(max(selectSize * 0.15f, 16.0f), selectSize * 0.5f);
    const float digitBoxPad = digitBoxHeight * 0.15f;
    const float digitHeight = digitBoxHeight * 0.75f;
    const float digitPad = digitBoxHeight * 0.1f; (void)digitPad; // Implicit as text centering is handled by gdip
    const float nameHeight = pad * 0.6f;
    const float namePad = pad * 0.2f;
    const float pathThickness = 2.0f;

    float x = pad;
    float y = pad;

    // Resources
    GpFont* fontName = NULL;
    GpFontFamily* pFontFamily = NULL;
    {
        GpFontCollection* fc = NULL;
        ASSERT(Ok == GdipNewInstalledFontCollection(&fc));
        ASSERT(Ok == GdipCreateFontFamilyFromName(L"Segoe UI", fc, &pFontFamily));
        ASSERT(Ok == GdipCreateFont(pFontFamily, nameHeight, FontStyleRegular, (int)MetafileFrameUnitPixel, &fontName));
    }



    // Selection box
    {
        const uint32_t selIdx = (uint32_t)appData->_Selection;
        const uint32_t mouseSelIdx = (uint32_t)appData->_MouseSelection;

        {
            RectF selRect = { pad + containerSize * mouseSelIdx + padSelect, pad + padSelect, selectSize, selectSize };
            DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushBgHighlight, &selRect, 10);
        }

        {
            RectF selRect = { pad + containerSize * selIdx + padSelect, pad + padSelect, selectSize, selectSize };
            COLORREF cr = pGraphRes->_TextColor;
            ARGB gdipColor = cr | 0xFF000000;
            GpPen* pPen;
            GdipCreatePen1(gdipColor, 2, UnitPixel, &pPen);
            DrawRoundedRect(pGraphics, pPen, NULL, &selRect, 10);
            GdipDeletePen(pPen);
        }
    }

    for (uint32_t i = 0; i < appData->_WinGroups._Size; i++)
    {
        const SWinGroup* pWinGroup = &appData->_WinGroups._Data[i];

        // Icon
        // TODO: Check histogram and invert (or another filter) if background is similar
        // https://learn.microsoft.com/en-us/windows/win32/api/gdiplusheaders/nf-gdiplusheaders-bitmap-gethistogram
        // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-remap-table-use
        // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-matrix-to-transform-a-single-color-use
        // Also check palette to see if monochrome
        if (pWinGroup->_IconBitmap)
        {
            unsigned int bitmapWidth; 
            GdipGetImageWidth(pWinGroup->_IconBitmap, &bitmapWidth);
            // If very low res, use nearest neighbor upscale
            if (bitmapWidth < 64)
            {
                InterpolationMode backupInterpMode = InterpolationModeInvalid;
                GdipGetInterpolationMode(pGraphics, &backupInterpMode);
                GdipSetInterpolationMode(pGraphics, InterpolationModeNearestNeighbor); // InterpolationModeHighQualityBicubic
                // Ensure draw size is a multiple of bitmap size so upscaled pixel size is constant.
                unsigned int ratio = iconSize / bitmapWidth;
                unsigned int targetIconSize = ratio * bitmapWidth;
                float extraPad = (float)(iconSize - targetIconSize) / 2.0f;
                GdipDrawImageRectI(pGraphics, pWinGroup->_IconBitmap, x + padIcon + extraPad, y + padIcon + extraPad, targetIconSize, targetIconSize);
                GdipSetInterpolationMode(pGraphics, backupInterpMode);
            }
            else
            
                GdipDrawImageRectI(pGraphics, pWinGroup->_IconBitmap, x + padIcon, y + padIcon, iconSize, iconSize);
        }

        // Digit
        if (appData->_Config._AppSwitcherMode == AppSwitcherModeApp && pWinGroup->_WindowCount > 1)
        {
            WCHAR str[] = L"\0\0";
            const uint32_t winCount = min(pWinGroup->_WindowCount, 99);
            const uint32_t digitsCount = winCount > 9 ? 2 : 1;
            const float w = digitBoxHeight;
            const float h = digitBoxHeight;
            const float p = digitBoxPad + pathThickness;
            RectF r = {
                (x + padSelect + selectSize - p - w),
                (y + padSelect + selectSize - p - h),
                (w),
                (h) };
            r.X = round(r.X);
            r.Y = round(r.Y);
            r.Width = round(r.Width);
            r.Height = round(r.Height);

            swprintf(str, 3, L"%i", winCount);
            // Invert text / bg brushes 
            DrawRoundedRect(pGraphics, NULL, pGraphRes->_pBrushText, &r, 5);

            GpPath* pPath;
            ASSERT(Ok == GdipCreatePath(FillModeAlternate, &pPath));
            RectF rr = {0, 0, 0, 0};
            ASSERT(Ok == GdipAddPathString(pPath, str, digitsCount, pFontFamily, FontStyleBold, digitHeight * (digitsCount > 1 ? 0.8f : 1.0f), &rr, pGraphRes->_pFormat));
            ASSERT(Ok == GdipGetPathWorldBounds(pPath, &rr, NULL, NULL));
            rr.X = round(rr.X);
            rr.Y = round(rr.Y);
            rr.Width = round(rr.Width);
            rr.Height = round(rr.Height);
            GpMatrix* mat;
            ASSERT(Ok == GdipCreateMatrix2(1.0f, 0.0f, 0.0f, 1.0f, (r.X - rr.X + (r.Width - rr.Width) * 0.5f), (r.Y - rr.Y + (r.Height - rr.Height) * 0.5f), &mat));
            ASSERT(Ok == GdipTransformPath(pPath, mat));
            ASSERT(Ok == GdipDeleteMatrix(mat));
            ASSERT(Ok == GdipFillPath(pGraphics, pGraphRes->_pBrushBg, pPath));
            ASSERT(Ok == GdipDeletePath(pPath));

            // ASSERT(!GdipDrawString(pGraphics, str, digitsCount, digitsCount == 2 ? font2Digits : font1Digit, &r, pGraphRes->_pFormat, pGraphRes->_pBrushBg));
        }

        // Name
        const bool selected = i == (uint32_t)appData->_Selection;
        const bool mouseSelected = i == (uint32_t)appData->_MouseSelection;

        if (((selected || mouseSelected) && appData->_Config._DisplayName == DisplayNameSel) ||
            appData->_Config._DisplayName == DisplayNameAll)
        {
            //https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-obtaining-font-metrics-use
            const float h = nameHeight;
            const float p = namePad;
            const float w = containerSize - 2.0f * p;
            RectF r = {
                (int)(x + p),
                (int)(y + containerSize - padSelect + p),
                (int)(w),
                (int)(h) };
            static wchar_t name[MAX_PATH];
            const wchar_t* displayName = wcslen(pWinGroup->_Caption) > 0 ? pWinGroup->_Caption : pWinGroup->_AppName;
            int count = wcslen(displayName);
            if (count != 0)
            {
                RectF rout;
                int maxCount = 0;
                GdipMeasureString(pGraphics, displayName, count, fontName, &r, pGraphRes->_pFormat, &rout, &maxCount, 0);
                wcsncpy(name, displayName, min(maxCount, count));
                if (count > maxCount)
                {
                    wcscpy(&name[maxCount - 3], L"...");
                    count = maxCount;
                }
                GdipDrawString(pGraphics, name, count, fontName, &r, pGraphRes->_pFormat, pGraphRes->_pBrushText);
            }
        }

        x += containerSize;
    }
    BitBlt(GetDC(hwnd), clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, pGraphRes->_DC, 0, 0, SRCCOPY);

    // Always restore old bitmap (see fn doc)
    SelectObject(pGraphRes->_DC, oldBitmap);

    // Delete res.
    GdipDeleteFont(fontName);
    GdipDeleteFontFamily(pFontFamily);

    GdipDeleteGraphics(pGraphics);
}

LRESULT CALLBACK WorkerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static SAppData* appData = NULL;
    switch (uMsg)
    {
    case WM_CREATE:
    {
        appData = (SAppData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        return 0;
    }
    case MSG_APPLY_APP:
    {
        ApplySwitchApp(&appData->_WinGroups._Data[appData->_Selection], appData->_Config._RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    case MSG_APPLY_WIN:
    {
        ApplySwitchWin(appData->_CurrentWinGroup._Windows[appData->_Selection], appData->_Config._RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    case MSG_APPLY_APP_MOUSE:
    {
        ApplySwitchApp(&appData->_WinGroups._Data[appData->_MouseSelection], appData->_Config._RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT FocusWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    { 
        case WM_CREATE:
            return 0;
        case WM_PAINT:
            return 0;
        case WM_SIZE:
            return 0;
        case WM_DESTROY:
            return 0;
        case WM_SETFOCUS:
        //    printf("focus set\n");
            return 0;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam); 
    }
    return 0;
} 

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static SAppData* appData = NULL;
    static HWND focusWindows[MAX_WIN_GROUPS];
    switch (uMsg)
    {
    case WM_MOUSEMOVE:
    {
        if (!appData->_Config._Mouse)
            return 0;
        const int iconContainerSize = (int)appData->_Metrics._Container;
        const int pad = (int)appData->_Metrics._Pad;
        const int posX = GET_X_LPARAM(lParam);
        appData->_MouseSelection = min(max(0, (posX - pad) / iconContainerSize), (int)appData->_WinGroups._Size - 1);
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
        SetCapture(hwnd);
        // Otherwise cursor is busy on hover. I don't understand why.
        SetCursor(LoadCursor(NULL, IDC_ARROW));

        for (int i = 0; i < appData->_WinGroups._Size; i++)
        {
            const int iconContainerSize = (int)appData->_Metrics._Container;
            const int pad = (int)appData->_Metrics._Pad;
            int x = pad + i * (iconContainerSize);
            focusWindows[i] = CreateWindowEx(0, FOCUS_CLASS_NAME, NULL,
                WS_CHILD /* | WS_VISIBLE */,
                x, pad, iconContainerSize, iconContainerSize,
                hwnd, NULL, appData->_Instance, NULL);
        }

        return 0;
    }
    case MSG_FOCUS:
    {
        // uia set focus here gives inconsistent app behavior IDK why.
        // UIASetFocus(focusWindows[appData->_Selection]);
        SetFocus(focusWindows[appData->_Selection]);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (!IsInside(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), hwnd))
        {
            appData->_Mode = ModeNone;
            DestroyWin(&appData->_MainWin);
            ClearWinGroupArr(&appData->_WinGroups);
            return 0;
        }
        if (!appData->_Config._Mouse)
            return 0;
#ifdef ASYNC_APPLY
        ApplyWithTimeout(appData, MSG_APPLY_APP_MOUSE);
#else
        ApplySwitchApp(&appData->_WinGroups._Data[appData->_MouseSelection]);
#endif
        appData->_Mode = ModeNone;
        appData->_Selection = 0;
        appData->_MouseSelection = 0;
        DestroyWin(&appData->_MainWin);
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
        RECT clientRect;
        ASSERT(GetClientRect(hwnd, &clientRect));
        Draw(appData, ps.hdc, clientRect);
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

#if 0
static BOOL GetFirstWindow(HWND win, LPARAM lParam)
{
    *(HWND*)lParam = win;
    return false;
}

static HWND GetFirstChild(HWND win)
{
    HWND child = NULL;
    EnumChildWindows(win, GetFirstWindow, (LPARAM)&child);
    return child;
}
#endif

static void DeinitApp(SAppData* appData)
{
#ifdef ASYNC_APPLY
    ApplyWithTimeout(appData, MSG_APPLY_APP);
#else
    ApplySwitchApp(&appData->_WinGroups._Data[appData->_Selection]);
#endif
    appData->_Mode = ModeNone;
    appData->_Selection = 0;
    DestroyWin(&appData->_MainWin);
    ClearWinGroupArr(&appData->_WinGroups);
}

static void DeinitWin(SAppData* appData)
{
    appData->_Mode = ModeNone;
    appData->_Selection = 0;
}

int StartAltAppSwitcher(HINSTANCE hInstance)
{
    SetLastError(0);
    ASSERT(SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS));

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
        wc.lpfnWndProc   = MainWindowProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = MAIN_CLASS_NAME;
        wc.cbWndExtra = sizeof(SAppData*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc   = WorkerWindowProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = WORKER_CLASS_NAME;
        wc.cbWndExtra = sizeof(SAppData*);
        wc.style = 0;
        wc.hbrBackground = NULL;
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc   = DefWindowProc; /*FocusWindowProc;*/
        wc.hInstance     = hInstance;
        wc.lpszClassName = FOCUS_CLASS_NAME;
        wc.cbWndExtra = 0;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
        RegisterClass(&wc);
    }

    {
        _AppData._Mode = ModeNone;
        _AppData._Selection = 0;
        _AppData._Invert = false;
        _AppData._MainWin = NULL;
        _AppData._Instance = hInstance;
        _AppData._WinGroups._Size = 0;
        MEM_INIT(_AppData._WinGroups);
        // Hook needs globals
        _MainThread = GetCurrentThreadId();
        _KeyConfig = &_AppData._Config._Key;
        // Init. and loads config
        LoadConfig(&_AppData._Config);
        InitializeCriticalSection(&_AppData._WorkerCS);
        _AppData._MouseMonitor = NULL;

        // Patch only for runtime use. Do not patch if used for serialization.
#define PATCH_TILDE(key) key = key == VK_OEM_3 ? MapVirtualKey(41, MAPVK_VSC_TO_VK) : key;
    PATCH_TILDE(_AppData._Config._Key._AppHold);
    PATCH_TILDE(_AppData._Config._Key._AppSwitch);
    PATCH_TILDE(_AppData._Config._Key._WinHold);
    PATCH_TILDE(_AppData._Config._Key._WinSwitch);
    PATCH_TILDE(_AppData._Config._Key._Invert);
    PATCH_TILDE(_AppData._Config._Key._PrevApp);
#undef PATCH_TILDE

        _AppData._Elevated = false;
        {
            HANDLE tok;
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
            TOKEN_ELEVATION elTok;
            DWORD cbSize = sizeof(TOKEN_ELEVATION);
            GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
            _AppData._Elevated = elTok.TokenIsElevated;
            CloseHandle(tok);
        }

        char updater[MAX_PATH] = {};
        UpdaterPath(updater);
        if (_AppData._Config._CheckForUpdates && access(updater, F_OK) == 0)
        {
            STARTUPINFO si = {};
            PROCESS_INFORMATION pi = {};
            CreateProcess(NULL, updater, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
            &si, &pi);
        }
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

    ChangeWindowMessageFilter(MSG_RESTART_AAS, MSGFLT_ADD);
    ChangeWindowMessageFilter(MSG_CLOSE_AAS, MSGFLT_ADD);

    MSG msg = { };
    bool restartAAS = false;
    bool closeAAS = false;
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        switch (msg.message)
        {
        case MSG_NEXT_APP:
        {
            if (_AppData._Mode == ModeWin)
                DeinitWin(&_AppData);
            if (_AppData._Mode == ModeNone)
                InitializeSwitchApp(&_AppData);
            _AppData._Selection += _AppData._Invert ? -1 : 1;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            SendNotifyMessage(_AppData._MainWin, MSG_FOCUS, 0,0 );
            break;
        }
        case MSG_PREV_APP:
        {
            // Prev app does not have the ability to init the mode
            if (_AppData._Mode != ModeApp)
                break;
            _AppData._Selection += _AppData._Invert ? 1 : -1;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._WinGroups._Size);
            InvalidateRect(_AppData._MainWin, 0, FALSE);
            UpdateWindow(_AppData._MainWin);
            SendNotifyMessage(_AppData._MainWin, MSG_FOCUS, 0,0 );
            break;
        }
        case MSG_NEXT_WIN:
        {
            if (_AppData._Mode == ModeApp)
                break;
            if (_AppData._Mode == ModeNone)
                InitializeSwitchWin(&_AppData);
            _AppData._Selection += _AppData._Invert ? -1 : 1;
            _AppData._Selection = Modulo(_AppData._Selection, _AppData._CurrentWinGroup._WindowCount);
#ifdef ASYNC_APPLY
            ApplyWithTimeout(&_AppData, MSG_APPLY_WIN);
#else
            HWND win = _AppData._CurrentWinGroup._Windows[_AppData._Selection];
            ApplySwitchWin(win, appData->_Config._RestoreMinimizedWindows);
#endif
            break;
        }
        case MSG_DEINIT:
        {
            if (_AppData._Mode == ModeApp)
                DeinitApp(&_AppData);
            else if (_AppData._Mode == ModeWin)
                DeinitWin(&_AppData);
            break;
        }
        case MSG_CANCEL_APP:
        {
            _AppData._Mode = ModeNone;
            DestroyWin(&_AppData._MainWin);
            ClearWinGroupArr(&_AppData._WinGroups);
            break;
        }
        case MSG_RESTART_AAS:
        {
            restartAAS = true;
            break;
        }
        case MSG_CLOSE_AAS:
        {
            closeAAS = true;
            break;
        }
        case MSG_INVERT_PUSH:
        {
            _AppData._Invert = true;
            break;
        }
        case MSG_INVERT_REL:
        {
            _AppData._Invert = false;
            break;
        }
        case MSG_RESTORE_KEY:
        {
            RestoreKey(msg.wParam);
            break;
        }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (restartAAS || closeAAS)
            break;
    }

    {
        DeInitGraphicsResources(&_AppData._GraphicsResources);
    }

    GdiplusShutdown(gdiplusToken);
    UnregisterClass(MAIN_CLASS_NAME, hInstance);
    UnregisterClass(WORKER_CLASS_NAME, hInstance);

    if (restartAAS)
    {
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};

        char currentExe[MAX_PATH] = {};
        GetModuleFileName(NULL, currentExe, MAX_PATH);

        CreateProcess(NULL, currentExe, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
        &si, &pi);
    }
    return 0;
}
