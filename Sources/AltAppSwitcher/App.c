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
#include <time.h>
#include <Shobjidl.h>
#include "AppxPackaging.h"
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define ASYNC_APPLY

#define MAX_WIN_GROUPS 64u

typedef struct SWinGroup {
    char ModuleFileName[MAX_PATH];
    ATOM WinClass;
    wchar_t AppName[MAX_PATH];
    wchar_t Caption[MAX_PATH];
    HWND Windows[MAX_WIN_GROUPS];
    uint32_t WindowCount;
    GpBitmap* IconBitmap;
} SWinGroup;

typedef struct SWinArr {
    uint32_t Size;
    HWND* Data;
} SWinArr;

typedef struct SWinGroupArr {
    SWinGroup Data[MAX_WIN_GROUPS];
    uint32_t Size;
} SWinGroupArr;

typedef struct KeyState {
    bool InvertKeyDown;
    bool HoldWinDown;
    bool HoldAppDown;
} KeyState;

typedef struct SGraphicsResources {
    GpSolidFill* pBrushText;
    GpSolidFill* pBrushBg;
    GpSolidFill* pBrushBgHighlight;
    GpStringFormat* pFormat;
    COLORREF BackgroundColor;
    COLORREF HighlightBackgroundColor;
    COLORREF TextColor;
    HBITMAP Bitmap;
    HDC DC;
    HIMAGELIST ImageList;
    bool LightTheme;
} SGraphicsResources;

typedef struct Metrics {
    uint32_t WinPosX;
    uint32_t WinPosY;
    uint32_t WinX;
    uint32_t WinY;
    float Container;
    float Icon;
    float Pad;
    float DigitBoxHeight;
    float DigitBoxPad;
    float PathThickness;
} Metrics;

typedef enum Mode {
    ModeNone,
    ModeApp,
    ModeWin
} Mode;

typedef struct SUWPIconMapElement {
    wchar_t UserModelID[512];
    wchar_t Icon[MAX_PATH];
    wchar_t AppName[MAX_PATH];
} SUWPIconMapElement;

#define UWPICONMAPSIZE 16
typedef struct SUWPIconMap {
    SUWPIconMapElement Data[UWPICONMAPSIZE];
    uint32_t Head;
    uint32_t Count;
} SUWPIconMap;

typedef struct SAppData {
    HWND MainWin;
    HINSTANCE Instance;
    Mode Mode;
    bool Invert;
    int Selection;
    int MouseSelection;
    bool CloseHover;
    SGraphicsResources GraphicsResources;
    SWinGroupArr WinGroups;
    SWinGroup CurrentWinGroup;
    SUWPIconMap UWPIconMap;
    Config Config;
    Metrics Metrics;
    bool Elevated;
    CRITICAL_SECTION WorkerCS;
    HANDLE WorkerWin;
    HMONITOR MouseMonitor;
} SAppData;

typedef struct SFoundWin {
    HWND Data[64];
    uint32_t Size;
} SFoundWin;

static const struct KeyConfig* KeyConfig;
static DWORD MainThread;

// Main thread
#define MSG_INIT_APP (WM_USER + 1)
#define MSG_INIT_WIN (WM_USER + 2)
#define MSG_DEINIT (WM_USER + 3)
#define MSG_RESTORE_KEY (WM_USER + 4)

// Apply thread
#define MSG_APPLY_APP (WM_USER + 1)
#define MSG_APPLY_WIN (WM_USER + 2)
#define MSG_APPLY_APP_MOUSE (WM_USER + 3)

// Main window
#define MSG_FOCUS (WM_USER + 1)
#define MSG_REFRESH (WM_USER + 2)

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
        input.ki.wVk = KeyConfig->Invert;
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
    if (GetAsyncKeyState(VK_RCONTROL) & 0x8000) {
        // printf("need reset key 0\n");
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = VK_RCONTROL;
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        const UINT uSent = SendInput(1, &input, sizeof(INPUT));
        ASSERT(uSent == 1);
    }

    usleep(1000);
    if (GetKeyState(VK_RCONTROL) & 0x8000) {
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
        ASSERT(Ok == GdipCloneStringFormat(pGenericFormat, &pRes->pFormat));
        ASSERT(Ok == GdipSetStringFormatAlign(pRes->pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipSetStringFormatLineAlign(pRes->pFormat, StringAlignmentCenter));
        ASSERT(Ok == GdipSetStringFormatFlags(pRes->pFormat, StringFormatFlagsNoClip | StringFormatFlagsDisplayFormatControl));
    }
    // Colors
    {
        bool lightTheme = true;
        if (config->ThemeMode == ThemeModeAuto) {
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

            for (uint32_t i = 0; i < valueCount; i++) {
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

                if (!lstrcmpiA(name, "AppsUseLightTheme") && nameSize > 0) {
                    lightTheme = value;
                    break;
                }
            }

            RegCloseKey(key);
        } else {
            lightTheme = config->ThemeMode == ThemeModeLight;
        }

        // Colorref do not support alpha and high order bits MUST be 00
        // This is different from gdip "ARGB" type
        COLORREF darkColor = 0x002C2C2C;
        COLORREF lightColor = 0x00FFFFFF;
        pRes->LightTheme = lightTheme;
        if (lightTheme) {
            pRes->BackgroundColor = lightColor;
            pRes->HighlightBackgroundColor = lightColor - 0x00131313;
            pRes->TextColor = darkColor;
        } else {
            pRes->BackgroundColor = darkColor;
            pRes->HighlightBackgroundColor = darkColor + 0x00131313;
            pRes->TextColor = lightColor;
        }
    }
    // Brushes
    {
        ASSERT(Ok == GdipCreateSolidFill(pRes->BackgroundColor | 0xFF000000, &pRes->pBrushBg));
        ASSERT(Ok == GdipCreateSolidFill(pRes->HighlightBackgroundColor | 0xFF000000, &pRes->pBrushBgHighlight));
        ASSERT(Ok == GdipCreateSolidFill(pRes->TextColor | 0xFF000000, &pRes->pBrushText));
    }
}

static void DeInitGraphicsResources(SGraphicsResources* pRes)
{
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushText));
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushBg));
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushBgHighlight));
    ASSERT(Ok == GdipDeleteStringFormat(pRes->pFormat));
}

static const char* WindowsClassNamesToSkip[] = {
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
    if (strcmp("IME", className) != 0)
        return TRUE;
    (*(HWND*)lParam) = hwnd;
    return TRUE;
}

typedef struct SFindPIDEnumFnParams {
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
    if (strcmp("Windows.UI.Core.CoreWindow", className) != 0)
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

typedef struct SFindUWPChildParams {
    DWORD OutUWPPID;
    DWORD InHostPID;
} SFindUWPChildParams;

static BOOL FindUWPChild(HWND hwnd, LPARAM lParam)
{
    SFindUWPChildParams* pParams = (SFindUWPChildParams*)lParam;
    DWORD PID = 0;
    GetWindowThreadProcessId(hwnd, &PID);
    if (PID != pParams->InHostPID) {
        pParams->OutUWPPID = PID;
        return FALSE;
    }
    return TRUE;
}

static void FindActualPID(HWND hwnd, DWORD* PID)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    {
        wchar_t UMI[512];
        GetWindowThreadProcessId(hwnd, PID);
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *PID);
        uint32_t size = 512;
        BOOL isUWP = GetApplicationUserModelId(proc, &size, UMI) == ERROR_SUCCESS;
        CloseHandle(proc);
        if (isUWP) {
            return;
        }
    }

    if (!strcmp("ApplicationFrameWindow", className)) {
        {
            SFindUWPChildParams params;
            GetWindowThreadProcessId(hwnd, &(params.InHostPID));
            params.OutUWPPID = 0;
            EnumChildWindows(hwnd, FindUWPChild, (LPARAM)&params);
            if (params.OutUWPPID != 0) {
                *PID = params.OutUWPPID;
                return;
            }
        }
        {
            SFindPIDEnumFnParams params;
            params.InHostWindow = hwnd;
            params.OutPID = 0;

            EnumWindows(FindPIDEnumFn, (LPARAM)&params);

            *PID = params.OutPID;
            return;
        }
    }

    {
        GetWindowThreadProcessId(hwnd, PID);
        return;
    }
}

bool BelongsToCurrentDesktop(HWND window)
{
    IVirtualDesktopManager* vdm = NULL;
    (void)vdm;
    CoInitialize(NULL);
    CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_ALL, &IID_IVirtualDesktopManager, (void**)&vdm);

    if (!vdm) {
        CoUninitialize();
        return true;
    }

    WINBOOL isCurrent = true;
    IVirtualDesktopManager_IsWindowOnCurrentVirtualDesktop(vdm, window, &isCurrent);
    IVirtualDesktopManager_Release(vdm);
    CoUninitialize();
    return isCurrent;
}

static bool IsWindowOnMonitor(HWND hwnd, HMONITOR targetMonitor)
{
    RECT windowRect;

    // For minimized windows, use the restored position instead of current position
    if (IsIconic(hwnd)) {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(hwnd, &wp)) {
            windowRect = wp.rcNormalPosition;
        } else {
            // Fallback to GetWindowRect if GetWindowPlacement fails
            if (!GetWindowRect(hwnd, &windowRect))
                return false;
        }
    } else {
        if (!GetWindowRect(hwnd, &windowRect))
            return false;
    }

    // Use MonitorFromRect for more accurate monitor detection
    // This considers the window's entire area, not just center point
    HMONITOR windowMonitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONEAREST);

    // Use CompareObjectHandles if available (Windows 10+) for more robust comparison
    // Fall back to direct comparison for older Windows versions
    static HMODULE kernel32 = NULL;
    static BOOL(WINAPI * pCompareObjectHandles)(HANDLE, HANDLE) = NULL;
    static bool initialized = false;

    if (!initialized) {
        kernel32 = GetModuleHandleA("kernel32.dll");
        if (kernel32) {
            pCompareObjectHandles = (BOOL(WINAPI*)(HANDLE, HANDLE))
                GetProcAddress(kernel32, "CompareObjectHandles");
        }
        initialized = true;
    }

    if (pCompareObjectHandles) {
        return pCompareObjectHandles(windowMonitor, targetMonitor);
    }

    // Fallback to direct comparison for older Windows versions
    return windowMonitor == targetMonitor;
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
    if ((wi.dwExStyle & WS_EX_TOOLWINDOW))
        return false;
    if ((wi.dwExStyle & WS_EX_TOPMOST) && !(wi.dwExStyle & WS_EX_APPWINDOW))
        return false;

    // Start at the root owner
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    (void)owner;
    // const HWND parent = GetAncestor(hwnd, GA_PARENT); (void)parent;
    // const HWND dw = GetDesktopWindow(); (void)dw;
    // Taskbar window if: owner is self or WS_EX_APPWINDOW is set
    bool b = (wi.dwExStyle & WS_EX_APPWINDOW) != 0;
    (void)(b);
    bool isOwned = owner != hwnd && owner != NULL;
    if ((isOwned) && !(wi.dwExStyle & WS_EX_APPWINDOW))
        return false;

    if (appData->Config.DesktopFilter == DesktopFilterCurrent && !BelongsToCurrentDesktop(hwnd))
        return false;

    if (!IsWindowVisible(hwnd))
        return false;

    static char buf[512];
    GetClassName(hwnd, buf, 512);
    for (uint32_t i = 0; i < sizeof(WindowsClassNamesToSkip) / sizeof(WindowsClassNamesToSkip[0]); i++) {
        if (!strcmp(WindowsClassNamesToSkip[i], buf))
            return false;
    }
    WINBOOL cloaked = false;
    if (!strcmp(buf, "ApplicationFrameWindow"))
        DwmGetWindowAttribute(hwnd, (DWORD)DWMWA_CLOAKED, (PVOID)&cloaked, (DWORD)sizeof(cloaked));
    if (cloaked)
        return false;

    // Filter apps by monitor if enabled
    if (appData->Config.AppFilterMode == AppFilterModeMouseMonitor) {
        if (!IsWindowOnMonitor(hwnd, appData->MouseMonitor))
            return true;
    }

    if (ignoreMinimizedWindows) {
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

// https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/AppxPackingDescribeAppx/cpp/DescribeAppx.cpp
static void GetUWPIconAndAppName(HANDLE process, wchar_t* outIconPath, wchar_t* outAppName, SAppData* appData)
{
    static wchar_t userModelID[512];
    {
        uint32_t length = 512;
        GetApplicationUserModelId(process, &length, userModelID);
    }

    SUWPIconMap* iconMap = &appData->UWPIconMap;
    for (uint32_t i = 0; i < iconMap->Count; i++) {
        const uint32_t i0 = Modulo((int)(iconMap->Head - 1 - i), UWPICONMAPSIZE);
        if (wcscmp(iconMap->Data[i0].UserModelID, userModelID) != 0)
            continue;
        wcscpy(outIconPath, iconMap->Data[i0].Icon);
        wcscpy(outAppName, iconMap->Data[i0].AppName);
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
        while (hasApp) {
            static wchar_t* aumid = NULL;
            IAppxManifestApplicationsEnumerator_GetCurrent(appEnum, &app);
            IAppxManifestApplication_GetAppUserModelId(app, &aumid);
            if (!wcscmp(aumid, userModelID)) {
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
        if (logoProp == NULL || displayName == NULL) {
            CoUninitialize();
            return;
        }
    }
    for (uint32_t i = 0; logoProp[i] != L'\0'; i++) {
        if (logoProp[i] == L'\\')
            logoProp[i] = L'/';
    }

    {
        if (wcsstr(displayName, L"ms-resource:") == displayName) {
            LoadIndirectString(packagePath, pid[0].name, &displayName[12], outAppName);
        } else
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

    if (hFind == INVALID_HANDLE_VALUE) {
        CoUninitialize();
        return;
    }

    uint32_t maxSize = 0;
    bool foundAny = false;

    // https://learn.microsoft.com/en-us/windows/apps/design/style/iconography/app-icon-construction
    while (FindNextFileW(hFind, &findData) != 0) {
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
            if (at != NULL) {
                at += (sizeof(L"targetsize-") / sizeof(wchar_t)) - 1;
                targetsize = wcstol(at, NULL, 10);
            }
        }

        const bool lightUnplated = wcsstr(postLogoName, L"altform-lightunplated") != NULL;
        const bool unplated = wcsstr(postLogoName, L"altform-unplated") != NULL;
        const bool constrast = wcsstr(postLogoName, L"contrast") != NULL;
        const bool matchingTheme = !constrast && ((appData->GraphicsResources.LightTheme && lightUnplated) || (!appData->GraphicsResources.LightTheme && unplated));

        if (targetsize > maxSize || !foundAny || (targetsize == maxSize && matchingTheme)) {
            maxSize = targetsize;
            foundAny = true;
            wcscpy(outIconPath, parentDir);
            wcscat(outIconPath, L"/");
            wcscat(outIconPath, findData.cFileName);
        }
    }

    {
        wcscpy(iconMap->Data[iconMap->Head].UserModelID, userModelID);
        wcscpy(iconMap->Data[iconMap->Head].Icon, outIconPath);
        wcscpy(iconMap->Data[iconMap->Head].AppName, outAppName);
        iconMap->Count = min(iconMap->Count + 1, UWPICONMAPSIZE);
        iconMap->Head = Modulo((int)(iconMap->Head + 1), UWPICONMAPSIZE);
    }

    CoUninitialize();
}

#pragma pack(push)
#pragma pack(2)
typedef struct {
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
    (void)hModule;
    (void)lpType;
    (void)lpName;
    (void)lParam;
    if (IS_INTRESOURCE(lpName)) {
        *(char**)lParam = lpName;
    } else {
        strcpy_s(*(char**)lParam, sizeof(char) * 256, lpName);
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
    if (SUCCEEDED(res)) {
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
    for (wchar_t* p = temp; *p != L'\0'; p++) {
        if (*p == L'/')
            lastSlash = p;
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
        if (iconGrp == NULL) {
            FreeLibrary(module);
            return NULL;
        }
        HGLOBAL hGlobal = LoadResource(module, iconGrp);
        GRPICONDIR* iconGrpData = (GRPICONDIR*)LockResource(hGlobal);
        uint32_t resByteSize = 0;
        for (uint32_t i = 0; i < iconGrpData->idCount; i++) {
            const GRPICONDIRENTRY* entry = &iconGrpData->idEntries[i];
            if (entry->dwBytesInRes > resByteSize) {
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
        hbmMask = ii.hbmMask;
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
        const int iconSize = bm.bmWidth;
        GdipCreateBitmapFromScan0(iconSize, iconSize, 4 * iconSize, PixelFormat32bppARGB, NULL, &out);
        GpRect r = { 0, 0, iconSize, iconSize };
        BitmapData dstData = {};
        GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
        GetBitmapBits(hbm, (LONG)sizeof(uint32_t) * iconSize * iconSize, dstData.Scan0);
        // Check if color has non zero alpha (is there an alternative)
        unsigned int* ptr = (unsigned int*)dstData.Scan0;
        bool noAlpha = true;
        for (int i = 0; i < iconSize * iconSize; i++) {
            if (ptr[i] & 0xFF000000) {
                noAlpha = false;
                break;
            }
        }
        // If no alpha, init
        if (noAlpha && hbmMask != NULL && iconSize <= 256) {
            BITMAP bitmapMask = {};
            GetObject(hbmMask, sizeof(bitmapMask), (LPVOID)&bitmapMask);
            unsigned int maskByteSize = bitmapMask.bmWidthBytes * bitmapMask.bmHeight;
            static char maskData[256 * 256 * 1 / 8] = {};
            GetBitmapBits(hbmMask, (LONG)maskByteSize, maskData);
            for (int i = 0; i < iconSize * iconSize; i++) {
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

#if false
    HICON classIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    (void)classIcon;
#endif
    // LONG_PTR winProc = GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    // static char winProcStr[] = "FFFFFFFFFFFFFFFF";
    // sprintf(winProcStr, "%08lX", (unsigned long)winProc);
    // strcat(moduleFileName, winProcStr);

    SWinGroupArr* winAppGroupArr = &(appData->WinGroups);

    if (appData->Config.AppSwitcherMode == AppSwitcherModeApp) {
        for (uint32_t i = 0; i < winAppGroupArr->Size; i++) {
            SWinGroup* const group = &(winAppGroupArr->Data[i]);
            if (group->WinClass == winClass && !strcmp(group->ModuleFileName, moduleFileName)) {
                // Group found
                static wchar_t caption[MAX_PATH];
                caption[0] = L'\0';
                GetWindowTextW(hwnd, caption, MAX_PATH);
                if (wcscmp(caption, group->Caption) != 0) {
                    // If caption differs, set group caption to null string
                    group->Caption[0] = L'\0';
                }
                group->Windows[group->WindowCount++] = hwnd;
                return true;
            }
        }
    }

    // No group found
    SWinGroup* group = NULL;
    {
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        if (!process) {
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

        if (elevated && !appData->Elevated)
            return true;

        group = &winAppGroupArr->Data[winAppGroupArr->Size++];
        strcpy_s(group->ModuleFileName, sizeof(group->ModuleFileName), moduleFileName);
        group->WinClass = winClass;
        ASSERT(group->WindowCount == 0);

        // Icon
        ASSERT(group->IconBitmap == NULL);

#if false
        bool stdIcon = false;
        {
            HICON icon = ExtractIcon(process, group->ModuleFileName, 0);
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

        if (!isUWP) {
            group->IconBitmap = GetIconFromExe(group->ModuleFileName);
            group->AppName[0] = L'\0';
            static wchar_t exePath[MAX_PATH];
            size_t s = mbstowcs(exePath, group->ModuleFileName, MAX_PATH);
            (void)s;
            GetAppName(exePath, group->AppName);
        } else if (isUWP) {
            static wchar_t iconPath[MAX_PATH];
            iconPath[0] = L'\0';
            group->AppName[0] = L'\0';
            GetUWPIconAndAppName(process, iconPath, group->AppName, appData);
            GdipLoadImageFromFile(iconPath, &group->IconBitmap);
        }

        {
            group->Caption[0] = L'\0';
            GetWindowTextW(hwnd, group->Caption, MAX_PATH);
        }

        if (group->IconBitmap == NULL) {
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
                BITMAP bm = {};
                GetObject(hbm, sizeof(BITMAP), &bm);
                const uint32_t iconSize = bm.bmWidth;
                GdipCreateBitmapFromScan0((int)iconSize, (int)iconSize, (int)(4 * iconSize), PixelFormat32bppARGB, NULL, &out);
                GpRect r = { 0, 0, (int)iconSize, (int)iconSize };
                BitmapData dstData = {};
                GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
                GetBitmapBits(hbm, (LONG)(sizeof(uint32_t) * iconSize * iconSize), dstData.Scan0);
                GdipBitmapUnlockBits(out, &dstData);
            }

            DeleteObject(hbm);
            group->IconBitmap = out;
        }
        CloseHandle(process);
    }

    group->Windows[group->WindowCount++] = hwnd;
    return true;
}

static BOOL FillCurrentWinGroup(HWND hwnd, LPARAM lParam)
{
    SAppData* appData = (SAppData*)(lParam);
    if (!IsEligibleWindow(hwnd, appData, !appData->Config.RestoreMinimizedWindows))
        return true;
    DWORD PID = 0;
    FindActualPID(hwnd, &PID);
    SWinGroup* currentWinGroup = &appData->CurrentWinGroup;
    static char moduleFileName[512];
    GetProcessFileName(PID, moduleFileName);
    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run
    if (0 != strcmp(moduleFileName, currentWinGroup->ModuleFileName) || currentWinGroup->WinClass != winClass)
        return true;
    currentWinGroup->Windows[currentWinGroup->WindowCount] = hwnd;
    currentWinGroup->WindowCount++;
    return true;
}

static void ComputeMetrics(uint32_t iconCount, float scale, Metrics* metrics, bool monitorModeMouse)
{
    int monitorOffset[2] = { 0, 0 };
    int monitorSize[2] = { 0, 0 };

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
    float iconSize = (float)GetSystemMetrics(SM_CXICON) * scale;
    const float padRatio = max(0.25 * iconSize, 16.0f) / iconSize; // Keep room for app name
    const int sizeX = min(iconSize * ((int)iconCount * containerRatio + 2.0f * padRatio), screenWidth * 0.9);
    iconSize = ((float)sizeX / ((((float)iconCount * containerRatio) + (2.0f * padRatio))));
    const uint32_t halfSizeX = sizeX / 2;
    const uint32_t sizeY = (uint32_t)((1.0f * iconSize * containerRatio) + (2.0f * padRatio * iconSize));
    const uint32_t halfSizeY = sizeY / 2;
    metrics->WinPosX = centerX - halfSizeX + monitorOffset[0];
    metrics->WinPosY = centerY - halfSizeY + monitorOffset[1];
    metrics->WinX = sizeX;
    metrics->WinY = sizeY;
    metrics->Icon = iconSize;
    metrics->Container = iconSize * containerRatio;
    metrics->Pad = iconSize * padRatio;
    metrics->DigitBoxHeight = min(max(metrics->Container * 0.15f, 16.0f), metrics->Container * 0.5f); // Min size of 16 for text
    metrics->PathThickness = 2.0f;
    metrics->DigitBoxPad = (0.15f * metrics->DigitBoxHeight) + metrics->PathThickness;
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
    if (appData->MainWin)
        DestroyWin(&appData->MainWin);

    if (appData->WinGroups.Size == 0)
        return;

    ComputeMetrics(appData->WinGroups.Size, appData->Config.Scale, &appData->Metrics, appData->Config.MultipleMonitorMode == MultipleMonitorModeMouse);

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, // Optional window styles (WS_EX_)
        MAIN_CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP, // Window style
        // Pos and size
        (int)appData->Metrics.WinPosX,
        (int)appData->Metrics.WinPosY,
        (int)appData->Metrics.WinX,
        (int)appData->Metrics.WinY,
        NULL, // Parent window
        NULL, // Menu
        appData->Instance, // Instance handle
        appData // Additional application data
    );
    ASSERT(hwnd);

    // Needed for exact client area.
    RECT r = {
        (LONG)appData->Metrics.WinPosX,
        (LONG)appData->Metrics.WinPosY,
        (LONG)(appData->Metrics.WinPosX + appData->Metrics.WinX),
        (LONG)(appData->Metrics.WinPosY + appData->Metrics.WinY)
    };
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
    appData->MainWin = hwnd;

    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    RECT clientRect = { 0, 0, (LONG)appData->Metrics.WinX, (LONG)appData->Metrics.WinY };
    Draw(appData, GetDC(hwnd), clientRect);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    // AnimateWindow(hwnd, 1, AW_ACTIVATE | AW_BLEND);
}
static void ClearWinGroupArr(SWinGroupArr* winGroups);

static void InitializeSwitchApp(SAppData* appData)
{
    SWinGroupArr* pWinGroups = &(appData->WinGroups);
    ASSERT(pWinGroups);
    if (!pWinGroups)
        return;
    pWinGroups->Size = 0;

    // Get mouse monitor once if filtering by monitor is enabled
    if (appData->Config.AppFilterMode == AppFilterModeMouseMonitor) {
        POINT mousePos;
        if (GetCursorPos(&mousePos)) {
            appData->MouseMonitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTONEAREST);
        } else {
            // Fall back to primary monitor if GetCursorPos fails
            appData->MouseMonitor = MonitorFromPoint((POINT) { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        }
    } else {
        appData->MouseMonitor = NULL; // Explicitly set NULL when not filtering by monitor
    }
    EnumWindows(FillWinGroups, (LPARAM)appData);
    appData->Mode = ModeApp;
    appData->Selection = 0;
    appData->MouseSelection = 0;
    CreateWin(appData);
}

static void InitializeSwitchWin(SAppData* appData)
{
    HWND win = GetForegroundWindow();
    while (true) {
        if (!win || IsEligibleWindow(win, appData, false))
            break;
        win = GetParent(win);
    }
    if (!win)
        return;
    DWORD PID;
    FindActualPID(win, &PID);
    SWinGroup* pWinGroup = &(appData->CurrentWinGroup);
    GetProcessFileName(PID, pWinGroup->ModuleFileName);
    pWinGroup->WinClass = IsRunWindow(win) ? 0x8002 : 0; // Run
    pWinGroup->WindowCount = 0;
    if (appData->Config.AppSwitcherMode == AppSwitcherModeApp)
        EnumWindows(FillCurrentWinGroup, (LPARAM)appData);
    else {
        pWinGroup->Windows[0] = win;
        pWinGroup->WindowCount = 1;
    }
    appData->Selection = 0;
    appData->Mode = ModeWin;
}

static void ClearWinGroupArr(SWinGroupArr* winGroups)
{
    for (uint32_t i = 0; i < winGroups->Size; i++) {
        if (winGroups->Data[i].IconBitmap) {
            GdipDisposeImage(winGroups->Data[i].IconBitmap);
            winGroups->Data[i].IconBitmap = NULL;
        }
        winGroups->Data[i].WindowCount = 0;
        winGroups->Data[i].AppName[0] = L'\0';
        winGroups->Data[i].Caption[0] = L'\0';
    }
    winGroups->Size = 0;
}

static void RestoreWin(HWND win)
{
    if (!IsWindow(win))
        return;
    WINDOWPLACEMENT placement;
    placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(win, &placement);
    if (placement.showCmd == SW_SHOWMINIMIZED) {
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

typedef struct ApplySwitchAppData {
    HWND Data[64];
    unsigned int Count;
    DWORD _fgWinThread;
} ApplySwitchAppData;

static void ApplySwitchApp(const SWinGroup* winGroup, bool restoreMinimized)
{
    // Set focus for all win, not only the last one. This way when the active window is closed,
    // the second to last window of the group becomes the active one.
    HWND fgWin = GetForegroundWindow();
    DWORD curThread = GetCurrentThreadId();
    DWORD fgWinThread = GetWindowThreadProcessId(fgWin, NULL);
    (void)curThread;
    (void)fgWinThread;
    DWORD ret;

    int winCount = (int)winGroup->WindowCount;

    if (restoreMinimized) {
        for (int i = winCount - 1; i >= 0; i--) {
            const HWND win = winGroup->Windows[Modulo(i + 1, winCount)];
            RestoreWin(win);
        }
    }

    HWND prev = HWND_TOP;
    HDWP dwp = BeginDeferWindowPos((int)winGroup->WindowCount);
    ASSERT(dwp != 0);
    for (int i = winCount - 1; i >= 0; i--) {
        const HWND win = winGroup->Windows[Modulo(i + 1, winCount)];
        if (!IsWindow(win))
            continue;

        // This seems more consistent than SetFocus
        // Check if this works with focus when closing multiple win
        DWORD targetWinThread = GetWindowThreadProcessId(win, NULL);
        (void)targetWinThread;
        ret = AttachThreadInput(targetWinThread, curThread, TRUE);
        (void)ret;
        // VERIFY(ret != 0);

        dwp = DeferWindowPos(dwp, win, prev, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        VERIFY(dwp != 0);
        prev = win;
    }
    ret = EndDeferWindowPos(dwp);
    VERIFY(ret != 0);

#if false
    SetFocus(winGroup->Windows[0]);
#endif

    for (int i = winCount - 1; i >= 0; i--) {
        const HWND win = winGroup->Windows[Modulo(i + 1, winCount)];
        if (!IsWindow(win))
            continue;
        DWORD targetWinThread = GetWindowThreadProcessId(win, NULL);
        (void)targetWinThread;
        ret = AttachThreadInput(targetWinThread, curThread, FALSE);
        (void)ret;
    }
}

#ifdef ASYNC_APPLY
static DWORD WorkerThread(LPVOID data)
{
    SAppData* appData = (SAppData*)data;

    HANDLE window = CreateWindowEx(WS_EX_TOPMOST, WORKER_CLASS_NAME, NULL, WS_POPUP,
        0, 0, 0, 0, HWND_MESSAGE, NULL, appData->Instance, appData);
    (void)window;

    EnterCriticalSection(&appData->WorkerCS);
    appData->WorkerWin = window;
    LeaveCriticalSection(&appData->WorkerCS);
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnterCriticalSection(&appData->WorkerCS);
    appData->WorkerWin = NULL;
    LeaveCriticalSection(&appData->WorkerCS);
    return 0;
}

static void ApplyWithTimeout(SAppData* appData, unsigned int msg)
{
    EnterCriticalSection(&appData->WorkerCS);
    appData->WorkerWin = NULL;
    LeaveCriticalSection(&appData->WorkerCS);

    DWORD tid;
    HANDLE ht = CreateThread(NULL, 0, WorkerThread, (void*)appData, 0, &tid);
    ASSERT(ht != NULL);

    while (true) {
        if (TryEnterCriticalSection(&appData->WorkerCS)) {
            const bool initialized = appData->WorkerWin != NULL;
            LeaveCriticalSection(&appData->WorkerCS);
            if (initialized)
                break;
        }
        usleep(100);
    }

    HWND fgWin = GetForegroundWindow();
    DWORD fgWinThread = GetWindowThreadProcessId(fgWin, NULL);
    (void)fgWinThread;
    DWORD ret = 0;
    (void)ret;

    ret = SetForegroundWindow(appData->WorkerWin);
    VERIFY(ret != 0);

    SendNotifyMessage(appData->WorkerWin, msg, 0, 0);

    time_t start = time(NULL);
    while (true) {
        if (TryEnterCriticalSection(&appData->WorkerCS)) {
            const bool done = appData->WorkerWin == NULL;
            LeaveCriticalSection(&appData->WorkerCS);
            if (done)
                break;
        }
        time_t now = time(NULL);
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

LRESULT oldKbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.vkCode == KeyConfig->AppHold;
    const bool nextAppKey = kbStrut.vkCode == KeyConfig->AppSwitch;
    const bool prevAppKey = kbStrut.vkCode == KeyConfig->PrevApp;
    const bool winHoldKey = kbStrut.vkCode == KeyConfig->WinHold;
    const bool nextWinKey = kbStrut.vkCode == KeyConfig->WinSwitch;
    const bool invertKey = kbStrut.vkCode == KeyConfig->Invert;
    const bool tabKey = kbStrut.vkCode == VK_TAB;
    const bool shiftKey = kbStrut.vkCode == VK_LSHIFT;
    const bool escKey = kbStrut.vkCode == VK_ESCAPE;
    // Vim keys: h=left, j=up, k=down, l=right (and arrow keys)
    const bool vimNextKey = kbStrut.vkCode == 'L' || kbStrut.vkCode == 'J' || kbStrut.vkCode == VK_RIGHT || kbStrut.vkCode == VK_DOWN;
    const bool vimPrevKey = kbStrut.vkCode == 'H' || kbStrut.vkCode == 'K' || kbStrut.vkCode == VK_LEFT || kbStrut.vkCode == VK_UP;
    const bool isWatchedKey = appHoldKey || nextAppKey || prevAppKey || winHoldKey || nextWinKey || invertKey || tabKey || shiftKey || escKey || vimNextKey || vimPrevKey;
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
        const bool isWinHold = GetAsyncKeyState((SHORT)KeyConfig->WinHold) & 0x8000;
        const bool isAppHold = GetAsyncKeyState((SHORT)KeyConfig->AppHold) & 0x8000;
        // Vim/Arrow navigation
        const bool vimNext = vimNextKey && !rel;
        const bool vimPrev = vimPrevKey && !rel;

        // Denit.
        if (prevMode == ModeApp && appHoldRelease) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeWin && winHoldRelease) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->WinHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeApp && cancel) {
            mode = ModeNone;
            // PostThreadMessage(MainThread, MSG_CANCEL_APP, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        }

        if (nextApp && isAppHold) {
            mode = ModeApp;
            // PostThreadMessage(MainThread, MSG_NEXT_APP, 0, 0);
            bypassMsg = true;
        } else if (nextWin && isWinHold) {
            mode = ModeWin;
            // PostThreadMessage(MainThread, MSG_NEXT_WIN, 0, 0);
            bypassMsg = true;
        } else if (invertPush) {
            // PostThreadMessage(MainThread, MSG_INVERT_PUSH, 0, 0);
        } else if (invertRelease) {
            // PostThreadMessage(MainThread, MSG_INVERT_REL, 0, 0);
        }

        if (prevApp && isAppHold) // Not *else* if because the key can be shared
        {
            // PostThreadMessage(MainThread, MSG_PREV_APP, 0, 0);
            bypassMsg = true;
        }

        // Vim/Arrow key navigation (h/left/k/up = prev, l/right/j/down = next)
        if (vimNext && prevMode == ModeApp) {
            // PostThreadMessage(MainThread, MSG_NEXT_APP, 0, 0);
            bypassMsg = true;
        } else if (vimPrev && prevMode == ModeApp) {
            // PostThreadMessage(MainThread, MSG_PREV_APP, 0, 0);
            bypassMsg = true;
        } else if (vimNext && prevMode == ModeWin) {
            // PostThreadMessage(MainThread, MSG_NEXT_WIN, 0, 0);
            bypassMsg = true;
        } else if (vimPrev && prevMode == ModeWin) {
            // PostThreadMessage(MainThread, MSG_PREV_WIN, 0, 0);
            bypassMsg = true;
        }
    }

    if (bypassMsg)
        return 1;

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static LRESULT KbProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    const KBDLLHOOKSTRUCT kbStrut = *(KBDLLHOOKSTRUCT*)lParam;
    if (kbStrut.flags & LLKHF_INJECTED)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    const bool appHoldKey = kbStrut.vkCode == KeyConfig->AppHold;
    const bool nextAppKey = kbStrut.vkCode == KeyConfig->AppSwitch;
    const bool prevAppKey = kbStrut.vkCode == KeyConfig->PrevApp;
    const bool winHoldKey = kbStrut.vkCode == KeyConfig->WinHold;
    const bool nextWinKey = kbStrut.vkCode == KeyConfig->WinSwitch;
    const bool isWatchedKey = appHoldKey || nextAppKey || prevAppKey || winHoldKey || nextWinKey;
    if (!isWatchedKey)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    static Mode mode = ModeNone;

    const bool rel = kbStrut.flags & LLKHF_UP;

    // Update target app state
    bool bypassMsg = false;
    const Mode prevMode = mode;
    {
        const bool winHoldRelease = winHoldKey && rel;
        const bool appHoldRelease = appHoldKey && rel;
        const bool nextApp = nextAppKey && !rel;
        const bool nextWin = nextWinKey && !rel;
        const bool isWinHold = GetAsyncKeyState((SHORT)KeyConfig->WinHold) & 0x8000;
        const bool isAppHold = GetAsyncKeyState((SHORT)KeyConfig->AppHold) & 0x8000;

        // Denit.
        if ((prevMode == ModeApp && appHoldRelease)
            || (prevMode == ModeWin && winHoldRelease)) {
            mode = ModeNone;
            PostThreadMessage(MainThread, MSG_DEINIT, 0, 0);
            bypassMsg = true;
        }

        // Init
        if (prevMode == ModeNone && isAppHold && nextApp) {
            mode = ModeApp;
            PostThreadMessage(MainThread, MSG_INIT_APP, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->AppHold, 0);
            bypassMsg = true;
        } else if (prevMode == ModeNone && isWinHold && nextWin) {
            mode = ModeWin;
            PostThreadMessage(MainThread, MSG_INIT_WIN, 0, 0);
            PostThreadMessage(MainThread, MSG_RESTORE_KEY, KeyConfig->WinHold, 0);
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
    GdipAddPathArcI(pPath, (INT)l, (INT)t, (INT)di, (INT)di, 180, 90);
    GdipAddPathArcI(pPath, (INT)(r - di), (INT)t, (INT)di, (INT)di, 270, 90);
    GdipAddPathArcI(pPath, (INT)(r - di), (INT)(b - di), (INT)di, (INT)di, 360, 90);
    GdipAddPathArcI(pPath, (INT)l, (INT)(b - di), (INT)di, (INT)di, 90, 90);
    GdipAddPathLineI(pPath, (INT)l, (INT)(b - (di * 0.5)), (INT)l, (INT)(t + (di * 0.5)));
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

static void CloseButtonRect(float* outRect, const Metrics* m, uint32_t idx)
{
    const float w = m->DigitBoxHeight;
    const float p = m->DigitBoxPad;
    RectF r = {
        m->Pad + (m->Container * (float)(idx + 1)) - w - p,
        m->Pad + p,
        w,
        w
    };
    outRect[0] = r.X;
    outRect[1] = r.Y;
    outRect[2] = r.X + r.Width;
    outRect[3] = r.Y + r.Height;
}

static void Draw(SAppData* appData, HDC dc, RECT clientRect)
{
    ASSERT(appData);
    if (!appData)
        return;
    HWND hwnd = appData->MainWin;
    SGraphicsResources* pGraphRes = &appData->GraphicsResources;

    HANDLE oldBitmap = SelectObject(pGraphRes->DC, pGraphRes->Bitmap);
    ASSERT(oldBitmap != NULL);
    ASSERT(oldBitmap != HGDI_ERROR);

    HBRUSH bgBrush = CreateSolidBrush(pGraphRes->BackgroundColor);
    FillRect(pGraphRes->DC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(pGraphRes->DC, TRANSPARENT); // ?

    GpGraphics* pGraphics = NULL;
    ASSERT(Ok == GdipCreateFromHDC(pGraphRes->DC, &pGraphics));
    // gdiplus/gdiplusenums.h
    GdipSetSmoothingMode(pGraphics, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(pGraphics, PixelOffsetModeHighQuality);
    GdipSetInterpolationMode(pGraphics, InterpolationModeHighQualityBilinear); // InterpolationModeHighQualityBicubic
    GdipSetTextRenderingHint(pGraphics, TextRenderingHintClearTypeGridFit);

    const float containerSize = appData->Metrics.Container;
    const float iconSize = appData->Metrics.Icon;
    const float selectSize = containerSize;
    const float pad = appData->Metrics.Pad;
    const float padSelect = (containerSize - selectSize) * 0.5f;
    const float padIcon = (containerSize - iconSize) * 0.5f;
    const float digitHeight = appData->Metrics.DigitBoxHeight * 0.75f;
    const float nameHeight = pad * 0.6f;
    const float namePad = pad * 0.2f;

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
        const uint32_t selIdx = (uint32_t)appData->Selection;
        const uint32_t mouseSelIdx = (uint32_t)appData->MouseSelection;

        {
            RectF selRect = { pad + (containerSize * (float)mouseSelIdx) + padSelect, pad + padSelect, selectSize, selectSize };
            DrawRoundedRect(pGraphics, NULL, pGraphRes->pBrushBgHighlight, &selRect, 10);
        }

        {
            RectF selRect = { pad + (containerSize * (float)selIdx) + padSelect, pad + padSelect, selectSize, selectSize };
            COLORREF cr = pGraphRes->TextColor;
            ARGB gdipColor = cr | 0xFF000000;
            GpPen* pPen;
            GdipCreatePen1(gdipColor, 2, UnitPixel, &pPen);
            DrawRoundedRect(pGraphics, pPen, NULL, &selRect, 10);
            GdipDeletePen(pPen);
        }
    }

    for (uint32_t i = 0; i < appData->WinGroups.Size; i++) {
        const SWinGroup* pWinGroup = &appData->WinGroups.Data[i];

        // Icon
        // TODO: Check histogram and invert (or another filter) if background is similar
        // https://learn.microsoft.com/en-us/windows/win32/api/gdiplusheaders/nf-gdiplusheaders-bitmap-gethistogram
        // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-remap-table-use
        // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-using-a-color-matrix-to-transform-a-single-color-use
        // Also check palette to see if monochrome
        if (pWinGroup->IconBitmap) {
            unsigned int bitmapWidth;
            GdipGetImageWidth(pWinGroup->IconBitmap, &bitmapWidth);
            // If very low res, use nearest neighbor upscale
            if (bitmapWidth < 64) {
                InterpolationMode backupInterpMode = InterpolationModeInvalid;
                GdipGetInterpolationMode(pGraphics, &backupInterpMode);
                GdipSetInterpolationMode(pGraphics, InterpolationModeNearestNeighbor); // InterpolationModeHighQualityBicubic
                // Ensure draw size is a multiple of bitmap size so upscaled pixel size is constant.
                unsigned int ratio = (unsigned int)(iconSize / (float)bitmapWidth);
                unsigned int targetIconSize = ratio * bitmapWidth;
                float extraPad = (iconSize - (float)targetIconSize) / 2.0f;
                GdipDrawImageRectI(
                    pGraphics, pWinGroup->IconBitmap, (INT)(x + padIcon + extraPad),
                    (INT)(y + padIcon + extraPad), (INT)targetIconSize, (INT)targetIconSize);
                GdipSetInterpolationMode(pGraphics, backupInterpMode);
            } else
                GdipDrawImageRectI(pGraphics, pWinGroup->IconBitmap, (INT)(x + padIcon), (INT)(y + padIcon), (INT)iconSize, (INT)iconSize);
        }

        // Digit
        if (appData->Config.AppSwitcherMode == AppSwitcherModeApp && pWinGroup->WindowCount > 1) {
            WCHAR str[] = L"\0\0";
            const uint32_t winCount = min(pWinGroup->WindowCount, 99);
            const uint32_t digitsCount = winCount > 9 ? 2 : 1;
            const float w = appData->Metrics.DigitBoxHeight;
            const float h = appData->Metrics.DigitBoxHeight;
            const float p = appData->Metrics.DigitBoxPad;
            RectF r = {
                (x + padSelect + selectSize - p - w),
                (y + padSelect + selectSize - p - h),
                (w),
                (h)
            };
            r.X = roundf(r.X);
            r.Y = roundf(r.Y);
            r.Width = roundf(r.Width);
            r.Height = roundf(r.Height);

            size_t o = swprintf_s(str, 2, L"%i", winCount);
            (void)o;
            // Invert text / bg brushes
            DrawRoundedRect(pGraphics, NULL, pGraphRes->pBrushText, &r, 5);

            GpPath* pPath;
            ASSERT(Ok == GdipCreatePath(FillModeAlternate, &pPath));
            RectF rr = { 0, 0, 0, 0 };
            ASSERT(Ok == GdipAddPathString(pPath, str, digitsCount, pFontFamily, FontStyleBold, digitHeight * (digitsCount > 1 ? 0.8f : 1.0f), &rr, pGraphRes->pFormat));
            ASSERT(Ok == GdipGetPathWorldBounds(pPath, &rr, NULL, NULL));
            rr.X = roundf(rr.X);
            rr.Y = roundf(rr.Y);
            rr.Width = roundf(rr.Width);
            rr.Height = roundf(rr.Height);
            GpMatrix* mat;
            ASSERT(Ok == GdipCreateMatrix2(1.0f, 0.0f, 0.0f, 1.0f, (r.X - rr.X) + ((r.Width - rr.Width) * 0.5f), (r.Y - rr.Y) + ((r.Height - rr.Height) * 0.5f), &mat));
            ASSERT(Ok == GdipTransformPath(pPath, mat));
            ASSERT(Ok == GdipDeleteMatrix(mat));
            ASSERT(Ok == GdipFillPath(pGraphics, pGraphRes->pBrushBg, pPath));
            ASSERT(Ok == GdipDeletePath(pPath));

            // ASSERT(!GdipDrawString(pGraphics, str, digitsCount, digitsCount == 2 ? font2Digits : font1Digit, &r, pGraphRes->pFormat, pGraphRes->pBrushBg));
        }

        // Name
        const bool selected = i == (uint32_t)appData->Selection;
        const bool mouseSelected = i == (uint32_t)appData->MouseSelection;

        if (((selected || mouseSelected) && appData->Config.DisplayName == DisplayNameSel) || appData->Config.DisplayName == DisplayNameAll) {
            // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-obtaining-font-metrics-use
            const float h = nameHeight;
            const float p = namePad;
            const float w = containerSize - (2.0f * p);
            RectF r = {
                (float)(int)(x + p),
                (float)(int)(y + containerSize - padSelect + p),
                (float)(int)(w),
                (float)(int)(h)
            };
            static wchar_t name[MAX_PATH];
            const wchar_t* displayName = wcslen(pWinGroup->Caption) > 0 ? pWinGroup->Caption : pWinGroup->AppName;
            int count = (int)wcslen(displayName);
            if (count != 0) {
                RectF rout;
                int maxCount = 0;
                GdipMeasureString(pGraphics, displayName, count, fontName, &r, pGraphRes->pFormat, &rout, &maxCount, 0);
                wcsncpy(name, displayName, min(maxCount, count));
                if (count > maxCount) {
                    wcscpy(&name[maxCount - 3], L"...");
                    count = maxCount;
                }
                GdipDrawString(pGraphics, name, count, fontName, &r, pGraphRes->pFormat, pGraphRes->pBrushText);
            }
        }

        x += containerSize;
    }

    // Close button
    {
        const uint32_t mouseSelIdx = (uint32_t)appData->MouseSelection;
        float r0[4];
        CloseButtonRect(r0, &appData->Metrics, mouseSelIdx);
        const float w0 = r0[2] - r0[0];
        const float w1 = w0 * 0.5f;
        float r1[4];
        r1[0] = r0[0] + (w1 * 0.5f);
        r1[1] = r0[1] + (w1 * 0.5f);
        r1[2] = r0[2] - (w1 * 0.5f);
        r1[3] = r0[3] - (w1 * 0.5f);
        r1[0] = roundf(r1[0]);
        r1[1] = roundf(r1[1]);
        r1[2] = roundf(r1[2]);
        r1[3] = roundf(r1[3]);
        if (appData->CloseHover) {
            RectF _r0 = { r0[0], r0[1], w0, w0 };
            _r0.X = roundf(_r0.X);
            _r0.Y = roundf(_r0.Y);
            _r0.Width = roundf(_r0.Width);
            _r0.Height = roundf(_r0.Height);
            DrawRoundedRect(pGraphics, NULL, pGraphRes->pBrushText, &_r0, 5);

            GpPen* pPen;
            GdipCreatePen2(pGraphRes->pBrushBg, 2, UnitPixel, &pPen);
            GdipDrawLineI(pGraphics, pPen, (int)r1[0], (int)r1[1], (int)r1[2], (int)r1[3]);
            GdipDrawLineI(pGraphics, pPen, (int)r1[2], (int)r1[1], (int)r1[0], (int)r1[3]);
            GdipDeletePen(pPen);
        } else {
            GpPen* pPen;
            GdipCreatePen2(pGraphRes->pBrushText, 2, UnitPixel, &pPen);
            GdipDrawLineI(pGraphics, pPen, (int)r1[0], (int)r1[1], (int)r1[2], (int)r1[3]);
            GdipDrawLineI(pGraphics, pPen, (int)r1[2], (int)r1[1], (int)r1[0], (int)r1[3]);
            GdipDeletePen(pPen);
        }
    }

    BitBlt(GetDC(hwnd), clientRect.left, clientRect.top, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top, pGraphRes->DC, 0, 0, SRCCOPY);

    // Always restore old bitmap (see fn doc)
    SelectObject(pGraphRes->DC, oldBitmap);

    // Delete res.
    GdipDeleteFont(fontName);
    GdipDeleteFontFamily(pFontFamily);

    GdipDeleteGraphics(pGraphics);
}

LRESULT CALLBACK WorkerWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static SAppData* appData = NULL;
    switch (uMsg) {
    case WM_CREATE: {
        appData = (SAppData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        return 0;
    }
    case MSG_APPLY_APP: {
        ASSERT(appData);
        if (!appData)
            return 0;
        ApplySwitchApp(&appData->WinGroups.Data[appData->Selection], appData->Config.RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    case MSG_APPLY_WIN: {
        ASSERT(appData);
        if (!appData)
            return 0;
        ApplySwitchWin(appData->CurrentWinGroup.Windows[appData->Selection], appData->Config.RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    case MSG_APPLY_APP_MOUSE: {
        ASSERT(appData);
        if (!appData)
            return 0;
        ApplySwitchApp(&appData->WinGroups.Data[appData->MouseSelection], appData->Config.RestoreMinimizedWindows);
        PostQuitMessage(0);
        return 0;
    }
    default: {
        break;
    }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int ProcessKeys(SAppData* appData, UINT uMsg, WPARAM wParam)
{
    switch (uMsg) {
    case WM_SYSKEYUP:
    case WM_KEYUP: {
        printf("HERE\n");
        return 0;
    }
    default:
        break;
    }
    return 1;
}

LRESULT FocusWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static SAppData* appData = NULL;

    if (ProcessKeys(appData, uMsg, wParam) == 0)
        return 0;

    switch (uMsg) {
    case WM_CREATE:
        appData = (SAppData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
    default:
        break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

typedef struct CloseThreadData {
    HWND Win[MAX_WIN_GROUPS];
    uint32_t Count;
    HWND MainWin;
} CloseThreadData;

static DWORD CloseThread(LPVOID data)
{
    const CloseThreadData* d = (const CloseThreadData*)data;
    for (int i = 0; i < d->Count; i++) {
        const HWND win = d->Win[i];
        while (IsWindow(win)) {
            usleep(1000);
        }
    }
    PostMessage(d->MainWin, MSG_REFRESH, 0, 0);
    return 0;
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static SAppData* appData = NULL;
    static HWND focusWindows[MAX_WIN_GROUPS] = {};

    if (ProcessKeys(appData, uMsg, wParam) == 0)
        return 0;

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        ASSERT(appData);
        if (!appData)
            return 0;
        if (!appData->Config.Mouse)
            return 0;
        const int iconContainerSize = (int)appData->Metrics.Container;
        const int pad = (int)appData->Metrics.Pad;
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        appData->MouseSelection = min(max(0, (x - pad) / iconContainerSize), (int)appData->WinGroups.Size - 1);
        float r[4];
        CloseButtonRect(r, &appData->Metrics, appData->MouseSelection);
        appData->CloseHover = x < (int)r[2] && x > (int)r[0] && y > (int)r[1] && y < (int)r[3];
        InvalidateRect(appData->MainWin, 0, FALSE);
        UpdateWindow(appData->MainWin);
        return 0;
    }
    case WM_CREATE: {
        appData = (SAppData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        ASSERT(appData);
        if (!appData)
            return 0;
        {
            RECT clientRect;
            ASSERT(GetWindowRect(hwnd, &clientRect));

            HDC winDC = GetDC(hwnd);
            ASSERT(winDC);
            appData->GraphicsResources.DC = CreateCompatibleDC(winDC);
            ASSERT(appData->GraphicsResources.DC != NULL);
            appData->GraphicsResources.Bitmap = CreateCompatibleBitmap(
                winDC,
                clientRect.right - clientRect.left,
                clientRect.bottom - clientRect.top);
            ASSERT(appData->GraphicsResources.Bitmap != NULL);
            ReleaseDC(hwnd, winDC);
        }
        SetCapture(hwnd);
        // Otherwise cursor is busy on hover. I don't understand why.
        SetCursor(LoadCursor(NULL, IDC_ARROW));

        if (!appData->Config.DebugDisableIconFocus) {
            for (int i = 0; i < appData->WinGroups.Size; i++) {
                const int iconContainerSize = (int)appData->Metrics.Container;
                const int pad = (int)appData->Metrics.Pad;
                int x = pad + (i * iconContainerSize);
                focusWindows[i] = CreateWindowEx(0, FOCUS_CLASS_NAME, NULL,
                    WS_CHILD /* | WS_VISIBLE */,
                    x, pad, iconContainerSize, iconContainerSize,
                    hwnd, NULL, appData->Instance, appData);
            }
        }
        InvalidateRect(hwnd, 0, FALSE);
        UpdateWindow(hwnd);
        return 0;
    }
    case MSG_FOCUS: {
        ASSERT(appData);
        if (!appData)
            return 0;
        // uia set focus here gives inconsistent app behavior IDK why.
        // UIASetFocus(focusWindows[appData->Selection]);
        if (!appData->Config.DebugDisableIconFocus) {
            SetFocus(focusWindows[appData->Selection]);
            return 0;
        }
        break;
    }
    case MSG_REFRESH: {
        ASSERT(appData);
        if (!appData)
            return 0;
        ClearWinGroupArr(&appData->WinGroups);
        InitializeSwitchApp(appData);
        return 0;
    }
    case WM_LBUTTONUP: {
        ASSERT(appData);
        if (!appData)
            return 0;
        if (!IsInside(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), hwnd)) {
            appData->Mode = ModeNone;
            DestroyWin(&appData->MainWin);
            ClearWinGroupArr(&appData->WinGroups);
            return 0;
        }
        if (!appData->Config.Mouse)
            return 0;

        if (appData->CloseHover) {
            static HANDLE ht = 0;
            static CloseThreadData ctd = {};
            if (ht)
                TerminateThread(ht, 0);
            ctd.Count = 0;

            ctd.MainWin = hwnd;
            const SWinGroup* winGroup = &appData->WinGroups.Data[appData->MouseSelection];
            for (int i = 0; i < winGroup->WindowCount; i++) {
                const HWND win = winGroup->Windows[i];
                ctd.Count++;
                ctd.Win[i] = win;
            }

            DWORD tid;
            ht = CreateThread(NULL, 0, CloseThread, (void*)&ctd, 0, &tid);

            for (int i = 0; i < winGroup->WindowCount; i++) {
                const HWND win = winGroup->Windows[i];
                PostMessage(win, WM_CLOSE, 0, 0);
            }

            return 0;
        }
#ifdef ASYNC_APPLY
        ApplyWithTimeout(appData, MSG_APPLY_APP_MOUSE);
#else
        ApplySwitchApp(&appData->WinGroups.Data[appData->MouseSelection]);
#endif
        appData->Mode = ModeNone;
        appData->Selection = 0;
        appData->MouseSelection = 0;
        appData->CloseHover = false;
        DestroyWin(&appData->MainWin);
        ClearWinGroupArr(&appData->WinGroups);
        return 0;
    }
    case WM_DESTROY: {
        ASSERT(appData);
        if (!appData)
            return 0;
        // Free hooks
        // {
        //     for (uint32_t i = 0; i < gCloseHookCount; i++)
        //     {
        //         ASSERT(gCloseHook[i]);
        //         UnhookWinEvent(gCloseHook[i]);
        //         gCloseHook[i] = 0;
        //     }
        //     gCloseHookCount = 0;
        //     g_MainWinForCloseHook = 0;
        // }
        DeleteDC(appData->GraphicsResources.DC);
        DeleteObject(appData->GraphicsResources.Bitmap);
        return 0;
    }
    case WM_PAINT: {
        ASSERT(appData);
        if (!appData)
            return 0;
        PAINTSTRUCT ps = {};
        if (BeginPaint(hwnd, &ps) == NULL) {
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
    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static DWORD KbHookCb(LPVOID param)
{
    (void)param;
    ASSERT(SetWindowsHookEx(WH_KEYBOARD_LL, KbProc, 0, 0));
    MSG msg = {};

    while (GetMessage(&msg, NULL, 0, 0) > 0) { }

    return (DWORD)0;
}

#if false
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
    ApplySwitchApp(&appData->WinGroups.Data[appData->Selection]);
#endif
    appData->Mode = ModeNone;
    appData->Selection = 0;
    DestroyWin(&appData->MainWin);
    ClearWinGroupArr(&appData->WinGroups);
}

static void DeinitWin(SAppData* appData)
{
    appData->Mode = ModeNone;
    appData->Selection = 0;
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

    static SAppData appData = {};

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = MainWindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = MAIN_CLASS_NAME;
        wc.cbWndExtra = sizeof(SAppData*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = WorkerWindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = WORKER_CLASS_NAME;
        wc.cbWndExtra = sizeof(SAppData*);
        wc.style = 0;
        wc.hbrBackground = NULL;
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = FocusWindowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = FOCUS_CLASS_NAME;
        wc.cbWndExtra = 0;
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
        RegisterClass(&wc);
    }

    {
        appData.Mode = ModeNone;
        appData.Selection = 0;
        appData.Invert = false;
        appData.MainWin = NULL;
        appData.Instance = hInstance;
        appData.WinGroups.Size = 0;
        // Hook needs globals
        MainThread = GetCurrentThreadId();
        KeyConfig = &appData.Config.Key;
        // Init. and loads config
        LoadConfig(&appData.Config);
        InitializeCriticalSection(&appData.WorkerCS);
        appData.MouseMonitor = NULL;

        // Patch only for runtime use. Do not patch if used for serialization.
#define PATCH_TILDE(key) (key) = (key) == VK_OEM_3 ? MapVirtualKey(41, MAPVK_VSC_TO_VK) : (key);
        PATCH_TILDE(appData.Config.Key.AppHold);
        PATCH_TILDE(appData.Config.Key.AppSwitch);
        PATCH_TILDE(appData.Config.Key.WinHold);
        PATCH_TILDE(appData.Config.Key.WinSwitch);
        PATCH_TILDE(appData.Config.Key.Invert);
        PATCH_TILDE(appData.Config.Key.PrevApp);
#undef PATCH_TILDE

        appData.Elevated = false;
        {
            HANDLE tok;
            OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
            TOKEN_ELEVATION elTok;
            DWORD cbSize = sizeof(TOKEN_ELEVATION);
            GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
            appData.Elevated = elTok.TokenIsElevated;
            CloseHandle(tok);
        }

        char updater[MAX_PATH] = {};
        UpdaterPath(updater);
        if (appData.Config.CheckForUpdates && access(updater, F_OK) == 0) {
            STARTUPINFO si = {};
            PROCESS_INFORMATION pi = {};
            CreateProcess(NULL, updater, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
                &si, &pi);
        }
        InitGraphicsResources(&appData.GraphicsResources, &appData.Config);
    }

    HANDLE threadKbHook = CreateRemoteThread(GetCurrentProcess(), NULL, 0, KbHookCb, (void*)&appData, 0, NULL);
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

    MSG msg = {};
    bool restartAAS = false;
    bool closeAAS = false;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        switch (msg.message) {
        case MSG_INIT_APP: {
            if (appData.Mode == ModeWin)
                DeinitWin(&appData);
            if (appData.Mode == ModeNone)
                InitializeSwitchApp(&appData);

            if (!appData.Config.DebugDisableIconFocus)
                SendNotifyMessage(appData.MainWin, MSG_FOCUS, 0, 0);
            break;
        }
        case MSG_INIT_WIN: {
            if (appData.Mode == ModeApp)
                DeinitApp(&appData);
            if (appData.Mode == ModeNone)
                InitializeSwitchWin(&appData);
            appData.Selection += appData.Invert ? -1 : 1;
            appData.Selection = Modulo(appData.Selection, (int)appData.CurrentWinGroup.WindowCount);
#ifdef ASYNC_APPLY
            ApplyWithTimeout(&appData, MSG_APPLY_WIN);
#else
            HWND win = appData.CurrentWinGroup.Windows[appData.Selection];
            ApplySwitchWin(win, appData->Config.RestoreMinimizedWindows);
#endif
            break;
        }
        case MSG_DEINIT: {
            if (appData.Mode == ModeApp)
                DeinitApp(&appData);
            else if (appData.Mode == ModeWin)
                DeinitWin(&appData);
            break;
        }
        case MSG_RESTART_AAS: {
            restartAAS = true;
            break;
        }
        case MSG_CLOSE_AAS: {
            closeAAS = true;
            break;
        }
        case MSG_RESTORE_KEY: {
            RestoreKey(msg.wParam);
            break;
        }
        default:
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (restartAAS || closeAAS)
            break;
    }

    {
        DeInitGraphicsResources(&appData.GraphicsResources);
    }

    GdiplusShutdown(gdiplusToken);
    UnregisterClass(MAIN_CLASS_NAME, hInstance);
    UnregisterClass(WORKER_CLASS_NAME, hInstance);

    if (restartAAS) {
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};

        char currentExe[MAX_PATH] = {};
        GetModuleFileName(NULL, currentExe, MAX_PATH);

        CreateProcess(NULL, currentExe, 0, 0, false, CREATE_NEW_PROCESS_GROUP, 0, 0,
            &si, &pi);
    }
    return 0;
}
