#define COBJMACROS
#include "AppMode.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
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
#include <uiautomationclient.h>
#include <gdiplus/gdiplusenums.h>
#include <PropKey.h>
#include <Shobjidl.h>
#include <shlobj.h>
#include "AppxPackaging.h"
#undef COBJMACROS
#include "Config/Config.h"
#include "Utils/Error.h"
#include "Utils/MessageDef.h"
#include "Utils/File.h"
#include "Messages.h"
#include "Common.h"

static LRESULT MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define ASYNC_APPLY

#define MAX_WIN_GROUPS 64u

typedef struct SWinGroup {
    wchar_t AUMID[MAX_PATH];
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

struct GraphicsResources {
    GpSolidFill* pBrushText;
    GpSolidFill* pBrushBg;
    GpSolidFill* pBrushBgHighlight;
    GpStringFormat* pFormat;
    COLORREF BackgroundColor;
    COLORREF HighlightBackgroundColor;
    COLORREF TextColor;
    HIMAGELIST ImageList;
    bool LightTheme;
};

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
struct UWPIconMap {
    SUWPIconMapElement Data[UWPICONMAPSIZE];
    uint32_t Head;
    uint32_t Count;
};

struct StaticData {
    struct GraphicsResources GraphicsResources;
    struct UWPIconMap UWPIconMap;
    bool Elevated;
    const struct Config* Config;
    HMODULE Instance;
    ULONG_PTR GdiplusToken;
};

static struct StaticData StaticData = {};

struct WindowData {
    HWND MainWin;
    int Selection;
    int MouseSelection;
    bool CloseHover;
    SWinGroupArr WinGroups;
    Metrics Metrics;
    HMONITOR MouseMonitor;
    HBITMAP Bitmap;
    HDC DC;
    struct StaticData* StaticData;
    HWND FocusWindows[MAX_WIN_GROUPS];
    unsigned int FocusWindowCount;
};

typedef struct SFoundWin {
    HWND Data[64];
    uint32_t Size;
} SFoundWin;

static void InitGraphicsResources(struct GraphicsResources* pRes, const Config* config)
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

static void DeinitGraphicsResources(struct GraphicsResources* pRes)
{
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushText));
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushBg));
    ASSERT(Ok == GdipDeleteBrush(pRes->pBrushBgHighlight));
    ASSERT(Ok == GdipDeleteStringFormat(pRes->pFormat));
}

void wcharToChar(char* dst, const wchar_t* src)
{
    while (*src != L'\0') {
        *dst = (char)*src;
        dst++;
        src++;
    }
    *dst = '\0';
}

static void CharToWChar(wchar_t* dst, const char* src)
{
    while (*src != '\0') {
        *dst = (wchar_t)*src;
        dst++;
        src++;
    }
    *dst = L'\0';
}

static BOOL GetWindowAUMI(HWND window, wchar_t* outUMI)
{
    PCWSTR aumid = (PCWSTR)GetPropW(window, L"AppUserModelID");
    (void)aumid;
    IPropertyStore* propertyStore;
    HRESULT res = SHGetPropertyStoreForWindow(window, &IID_IPropertyStore, (void**)&propertyStore);
    if (!SUCCEEDED(res))
        return false;
    PROPVARIANT pv = {};
    PropVariantInit(&pv);
    res = IPropertyStore_GetValue(propertyStore, &PKEY_AppUserModel_ID, &pv);
    IPropertyStore_Release(propertyStore);
    if (!SUCCEEDED(res)) {
        ASSERT(false);
        return false;
    }
    if (pv.vt == VT_LPWSTR) {
        wcscpy_s(outUMI, 512 * sizeof(char), pv.pwszVal);
        return true;
    }
    if (pv.vt == VT_LPSTR) {
        CharToWChar(outUMI, pv.pcVal);
        return true;
    }
    return false;
}

#if false
static BOOL GetWindowIconFromPropStore(HWND window, char* outIconPath)
{
    IPropertyStore* propertyStore;
    HRESULT res = SHGetPropertyStoreForWindow(window, &IID_IPropertyStore, (void**)&propertyStore);
    if (!SUCCEEDED(res))
        return false;
    PROPVARIANT pv = {};
    PropVariantInit(&pv);
    res = IPropertyStore_GetValue(propertyStore, &PKEY_AppUserModel_RelaunchIconResource, &pv);
    IPropertyStore_Release(propertyStore);
    if (!SUCCEEDED(res)) {
        return false;
    }
    if (pv.vt == VT_LPWSTR) {
        wcharToChar(outIconPath, pv.pwszVal);
        return true;
    }
    if (pv.vt == VT_LPSTR) {
        strcpy_s(outIconPath, 512 * sizeof(char), pv.pcVal);
        return true;
    }
    return false;
}
#endif

/*
static BOOL GetProcessAUMI_dbg(DWORD PID, char* outFileName)
{
    static HRESULT (*GetProcessExplicitAppUserModelID)(HANDLE, PWSTR*) = NULL;
    if (!GetProcessExplicitAppUserModelID) {
        HMODULE shell32 = LoadLibraryW(L"shell32.dll");
        if (!shell32)
            return false;
        GetProcessExplicitAppUserModelID = (HRESULT (*)(HANDLE, PWSTR*))GetProcAddress(shell32, "GetProcessExplicitAppUserModelID");
    }
    if (!GetProcessExplicitAppUserModelID)
        return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
    static wchar_t AUMI[512];
    HRESULT res = GetProcessExplicitAppUserModelID(process, (PWSTR*)&AUMI);
    CloseHandle(process);
    if (SUCCEEDED(res))
        return false;
    wcharToChar(outFileName, AUMI);
    return true;
}
*/

static BOOL GetProcessAUMI(DWORD PID, wchar_t* outAUMID)
{
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
    uint32_t size = 512;
    LONG res = GetApplicationUserModelId(process, &size, outAUMID);
    if (res == ERROR_SUCCESS) {
        CloseHandle(process);
        return true;
    }
    GetModuleFileNameExW(process, NULL, outAUMID, 512);
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

static void FindActualPID(HWND hwnd, DWORD* PID, DWORD* TID)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
    {
        *TID = GetWindowThreadProcessId(hwnd, PID);
        const HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *PID);
        uint32_t size = 512;
        wchar_t UMI[512];
        HRESULT res = GetApplicationUserModelId(proc, &size, UMI);
        CloseHandle(proc);
        if (SUCCEEDED(res)) {
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

static bool GetAppInfoFromMap(const struct UWPIconMap* map, const wchar_t* aumid, wchar_t* outIconPath, wchar_t* outAppName)
{
    const struct UWPIconMap* iconMap = map;
    for (uint32_t i = 0; i < iconMap->Count; i++) {
        const uint32_t i0 = Modulo((int)(iconMap->Head - 1 - i), UWPICONMAPSIZE);
        if (wcscmp(iconMap->Data[i0].UserModelID, aumid) != 0)
            continue;
        wcscpy(outIconPath, iconMap->Data[i0].Icon);
        wcscpy(outAppName, iconMap->Data[i0].AppName);
        return true;
    }
    return false;
}

static void StoreAppInfoToMap(struct UWPIconMap* map, const wchar_t* aumid, const wchar_t* iconPath, const wchar_t* appName)
{
    wcscpy(map->Data[map->Head].UserModelID, aumid);
    wcscpy(map->Data[map->Head].Icon, iconPath);
    wcscpy(map->Data[map->Head].AppName, appName);
    map->Count = min(map->Count + 1, UWPICONMAPSIZE);
    map->Head = Modulo((int)(map->Head + 1), UWPICONMAPSIZE);
}

static BOOL EndsWithW(const wchar_t* x, const wchar_t* y)
{
    return wcscmp(x + wcslen(x) - wcslen(y), y) == 0;
}

static bool FindLnk(const wchar_t* dirpath, const wchar_t* userModelID, wchar_t* outName, wchar_t* outIcon, uint32_t depth)
{
    WIN32_FIND_DATAW findData = {};
    HANDLE hFind = FindFirstFileW(dirpath, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        FindClose(hFind);
        return false;
    }

#define MAX_DEPTH 32

    if (depth >= MAX_DEPTH)
        return false;

    static wchar_t filePathArr[MAX_DEPTH][512];
    wchar_t* filePath = filePathArr[depth];

    bool found = false;
    do {
        // wprintf(L"File: %s\n", findData.cFileName);
        if (wcscmp(findData.cFileName, L"..") == 0 || wcscmp(findData.cFileName, L".") == 0)
            continue;
        filePath[0] = L'\0';
        wcscpy(filePath, dirpath);
        filePath[wcslen(filePath) - 1] = L'\0';
        wcscat(filePath, L"\\");
        wcscat(filePath, findData.cFileName);
        DWORD ftyp = GetFileAttributesW(filePath);
        if (ftyp == INVALID_FILE_ATTRIBUTES)
            continue;
        if (ftyp & FILE_ATTRIBUTE_DIRECTORY) {
            wcscat(filePath, L"\\*");
            found = FindLnk(filePath, userModelID, outName, outIcon, depth + 1);
            if (found)
                break;
            continue;
        }
        unsigned int nameLen = wcslen(findData.cFileName);
        if (nameLen > 4 && wcscmp(findData.cFileName + nameLen - 4, L".lnk") == 0) {
            // wprintf(L"Link: %s\n", filePath);
            IShellLinkW* shellLink;
            {
                HRESULT hr = CoCreateInstance(&CLSID_ShellLink, 0, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&shellLink);
                ASSERT(SUCCEEDED(hr));
            }
            IPersistFile* persistFile;
            {
                HRESULT hr = IShellLinkW_QueryInterface(shellLink, &IID_IPersistFile, (void**)&persistFile);
                ASSERT(SUCCEEDED(hr));
            }
            {
                HRESULT hr = IPersistFile_Load(persistFile, filePath, STGM_READ);
                ASSERT(SUCCEEDED(hr));
            }
            if (EndsWithW(userModelID, L".exe") || EndsWithW(userModelID, L".EXE")) {
                static wchar_t linkPath[512];
                IShellLinkW_GetPath(shellLink, linkPath, 512, NULL, 0);
                if (!wcscmp(linkPath, userModelID)) {
                    int idx = 0;
                    outIcon[0] = L'\0';
                    HRESULT hr = IShellLinkW_GetIconLocation(shellLink, outIcon, 512, &idx);
                    ASSERT(SUCCEEDED(hr));
                    if (outIcon[0] != L'\0') {
                        // wprintf(L"%s\n", outIcon);
                        wcscpy(outName, L"Unamed");
                        found = true;
                    }
                }
            } else {
                IPropertyStore* propertyStore;
                HRESULT hr = IShellLinkW_QueryInterface(shellLink, &IID_IPropertyStore, (void**)&propertyStore);
                ASSERT(SUCCEEDED(hr));
                PROPVARIANT pv = {};
                PropVariantInit(&pv);
                hr = IPropertyStore_GetValue(propertyStore, &PKEY_AppUserModel_ID, &pv);
                IPropertyStore_Release(propertyStore);
                if (!SUCCEEDED(hr)) {
                    ASSERT(false);
                    return false;
                }
                wchar_t foundAUMID[512] = {};
                if (pv.vt == VT_LPWSTR)
                    wcscpy_s(foundAUMID, 512 * sizeof(char), pv.pwszVal);
                if (pv.vt == VT_LPSTR)
                    CharToWChar(foundAUMID, pv.pcVal);
                if (!wcscmp(foundAUMID, userModelID)) {
                    int idx = 0;
                    IShellLinkW_GetIconLocation(shellLink, outIcon, 512, &idx);
                    wprintf(L"%s\n", outIcon);
                    wcscpy(outName, L"Unamed");
                    found = true;
                }
                {
                    HRESULT hr = IPersistFile_Release(persistFile);
                    ASSERT(SUCCEEDED(hr));
                }
            }
            {
                HRESULT hr = IShellLinkW_Release(shellLink);
                ASSERT(SUCCEEDED(hr));
            }
            if (found)
                break;
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    return found;
}

static bool GetAppInfoFromLnk(const wchar_t* userModelID, wchar_t* outIconPath, wchar_t* outAppName)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    static wchar_t* startMenu;
    static wchar_t search[512] = {};
    bool found = false;
    {
        SHGetKnownFolderPath(&FOLDERID_StartMenu, 0, 0, &startMenu);
        search[0] = L'\0';
        wcscpy(search, startMenu);
        wcscat(search, L"\\*");
        found = FindLnk(search, userModelID, outAppName, outIconPath, 0);
    }
    if (!found) {
        SHGetKnownFolderPath(&FOLDERID_CommonStartMenu, 0, 0, &startMenu);
        search[0] = L'\0';
        wcscpy(search, startMenu);
        wcscat(search, L"\\*");
        found = FindLnk(search, userModelID, outAppName, outIconPath, 0);
    }
    CoUninitialize();
    return found;
}

// https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/AppxPackingDescribeAppx/cpp/DescribeAppx.cpp
static void GetAppInfoFromManifest(HANDLE process, const wchar_t* userModelID, wchar_t* outIconPath, wchar_t* outAppName, struct WindowData* windowData)
{
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
        const bool matchingTheme = !constrast
            && ((windowData->StaticData->GraphicsResources.LightTheme && lightUnplated)
                || (!windowData->StaticData->GraphicsResources.LightTheme && unplated));

        if (targetsize > maxSize || !foundAny || (targetsize == maxSize && matchingTheme)) {
            maxSize = targetsize;
            foundAny = true;
            wcscpy(outIconPath, parentDir);
            wcscat(outIconPath, L"/");
            wcscat(outIconPath, findData.cFileName);
        }
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

static GpBitmap* GetIconFromExe(const wchar_t* exePath)
{
    HMODULE module = LoadLibraryExW(exePath, NULL, LOAD_LIBRARY_AS_DATAFILE);
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
    struct WindowData* windowData = (struct WindowData*)lParam;

    if (!IsEligibleWindow(hwnd, windowData->StaticData->Config, windowData->MouseMonitor, false))
        return true;

    DWORD PID = 0;
    DWORD TID = 0;

    FindActualPID(hwnd, &PID, &TID);
    static wchar_t AUMID[512];
    BOOL found = GetWindowAUMI(hwnd, AUMID);
    if (!found)
        GetProcessAUMI(PID, AUMID);

    // wprintf(L"%s\n", AUMID);
    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run

#if false
    HICON classIcon = (HICON)GetClassLongPtr(hwnd, GCLP_HICON);
    (void)classIcon;
#endif
    // LONG_PTR winProc = GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    // static char winProcStr[] = "FFFFFFFFFFFFFFFF";
    // sprintf(winProcStr, "%08lX", (unsigned long)winProc);
    // strcat(moduleFileName, winProcStr);

    SWinGroupArr* winAppGroupArr = &(windowData->WinGroups);

    if (windowData->StaticData->Config->AppSwitcherMode == AppSwitcherModeApp) {
        for (uint32_t i = 0; i < winAppGroupArr->Size; i++) {
            SWinGroup* const group = &(winAppGroupArr->Data[i]);
            if (group->WinClass == winClass && !wcscmp(group->AUMID, AUMID)) {
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

        if (elevated && !windowData->StaticData->Elevated)
            return true;

        group = &winAppGroupArr->Data[winAppGroupArr->Size++];
        wcscpy_s(group->AUMID, sizeof(group->AUMID), AUMID);
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
        ASSERT(group->IconBitmap == NULL);

#if false
        {
            static char iconPath[512] = {};
            BOOL success = GetWindowIconFromPropStore(hwnd, iconPath);
            if (success) {
                static wchar_t iconPathW[512];
                size_t s = mbstowcs(iconPathW, iconPath, 512);
                (void)s;
                GdipLoadImageFromFile(iconPathW, &group->IconBitmap);
                wcscpy_s(group->AppName, sizeof(group->AppName), L"Non initialized name");
            }
        }
#endif

        bool found = false;
        static wchar_t iconPath[MAX_PATH] = {};

        {
            found = GetAppInfoFromMap(&windowData->StaticData->UWPIconMap, group->AUMID, iconPath, group->AppName);
            if (found) {
                if (EndsWithW(iconPath, L".exe") || EndsWithW(iconPath, L".EXE"))
                    group->IconBitmap = GetIconFromExe(iconPath);
                else
                    GdipLoadImageFromFile(iconPath, &group->IconBitmap);
            }
        }

        if (!found) {
            found = GetAppInfoFromLnk(group->AUMID, iconPath, group->AppName);
            if (found) {
                if (EndsWithW(iconPath, L".exe") || EndsWithW(iconPath, L".EXE"))
                    group->IconBitmap = GetIconFromExe(iconPath);
                else
                    GdipLoadImageFromFile(iconPath, &group->IconBitmap);
                StoreAppInfoToMap(&windowData->StaticData->UWPIconMap, group->AUMID, iconPath, group->AppName);
            }
        }

        if (!found) {
            BOOL isUWP = false;
            {
                /*
                static wchar_t userModelID[256];
                userModelID[0] = L'\0';
                uint32_t userModelIDLength = 256;
                GetApplicationUserModelId(process, &userModelIDLength, userModelID);
                isUWP = userModelID[0] != L'\0';
                */
                UINT32 length = 0;
                LONG rc = GetPackageFullName(process, &length, 0);
                if (rc == ERROR_INSUFFICIENT_BUFFER)
                    isUWP = true;
            }

            if (!isUWP) {
                static wchar_t exePath[512] = {};
                const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
                GetModuleFileNameExW(process, NULL, exePath, 512);
                CloseHandle(process);
                group->IconBitmap = GetIconFromExe(exePath);
                group->AppName[0] = L'\0';
                GetAppName(exePath, group->AppName);
                StoreAppInfoToMap(&windowData->StaticData->UWPIconMap, group->AUMID, exePath, group->AppName);
            } else if (isUWP) {
                static wchar_t iconPath[MAX_PATH];
                iconPath[0] = L'\0';
                group->AppName[0] = L'\0';
                GetAppInfoFromManifest(process, group->AUMID, iconPath, group->AppName, windowData);
                GdipLoadImageFromFile(iconPath, &group->IconBitmap);
                StoreAppInfoToMap(&windowData->StaticData->UWPIconMap, group->AUMID, iconPath, group->AppName);
            }
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
static const char FOCUS_CLASS_NAME[] = "AASFocus";

static void DestroyWin(HWND* win)
{
    DestroyWindow(*win);
    *win = NULL;
}

static void Draw(struct WindowData* windowData, HDC dc, RECT clientRect);

static LRESULT FocusWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void AppModeInit(HINSTANCE instance, const struct Config* cfg)
{
    StaticData.Instance = instance;
    StaticData.Config = cfg;
    StaticData.Elevated = false;
    {
        HANDLE tok;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        TOKEN_ELEVATION elTok;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        GetTokenInformation(tok, TokenElevation, &elTok, sizeof(elTok), &cbSize);
        StaticData.Elevated = elTok.TokenIsElevated;
        CloseHandle(tok);
    }

    StaticData.GdiplusToken = 0;
    {
        GdiplusStartupInput gdiplusStartupInput = {};
        gdiplusStartupInput.GdiplusVersion = 1;
        uint32_t status = GdiplusStartup(&StaticData.GdiplusToken, &gdiplusStartupInput, NULL);
        ASSERT(!status);
    }

    InitGraphicsResources(&StaticData.GraphicsResources, cfg);

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = MainWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = MAIN_CLASS_NAME;
        wc.cbWndExtra = sizeof(void*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = FocusWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = FOCUS_CLASS_NAME;
        wc.cbWndExtra = sizeof(void*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
        RegisterClass(&wc);
    }
}

void AppModeDeinit()
{
    DeinitGraphicsResources(&StaticData.GraphicsResources);
    GdiplusShutdown(StaticData.GdiplusToken);
    UnregisterClass(MAIN_CLASS_NAME, StaticData.Instance);
    UnregisterClass(FOCUS_CLASS_NAME, StaticData.Instance);
}

HWND AppModeCreateWindow()
{
    DWORD fgwinthread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    AttachThreadInput(GetCurrentThreadId(), fgwinthread, TRUE);
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, // Optional window styles (WS_EX_)
        MAIN_CLASS_NAME, // Window class
        "", // Window text
        WS_BORDER | WS_POPUP, // Window style
        // Pos and size
        0,
        0,
        0,
        0,
        NULL, // Parent window
        NULL, // Menu
        StaticData.Instance, // Instance handle
        &StaticData // Additional application data
    );
    ASSERT(hwnd);
    SetForegroundWindow(hwnd);
    AttachThreadInput(GetCurrentThreadId(), fgwinthread, FALSE);
    return hwnd;
}

void AppModeDestroyWindow(HWND window)
{
    SendMessage(window, WM_CLOSE, (WPARAM)0, (LPARAM)0);
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

/*
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
*/

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
    // Setting focus forces focus to parent window, which is not desirable.
    // I.e., it overrides child window focus, like notepad++ text area.
    // Instead we should activate (or set foreground?).
    // Set window pos should already activate.
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

static void Draw(struct WindowData* windowData, HDC dc, RECT clientRect)
{
    ASSERT(windowData);
    if (!windowData)
        return;
    HWND hwnd = windowData->MainWin;
    struct GraphicsResources* pGraphRes = &windowData->StaticData->GraphicsResources;

    HANDLE oldBitmap = SelectObject(windowData->DC, windowData->Bitmap);
    ASSERT(oldBitmap != NULL);
    ASSERT(oldBitmap != HGDI_ERROR);

    HBRUSH bgBrush = CreateSolidBrush(pGraphRes->BackgroundColor);
    FillRect(windowData->DC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(windowData->DC, TRANSPARENT); // ?

    GpGraphics* pGraphics = NULL;
    ASSERT(Ok == GdipCreateFromHDC(windowData->DC, &pGraphics));
    // gdiplus/gdiplusenums.h
    GdipSetSmoothingMode(pGraphics, SmoothingModeAntiAlias);
    GdipSetPixelOffsetMode(pGraphics, PixelOffsetModeHighQuality);
    GdipSetInterpolationMode(pGraphics, InterpolationModeHighQualityBilinear); // InterpolationModeHighQualityBicubic
    GdipSetTextRenderingHint(pGraphics, TextRenderingHintClearTypeGridFit);

    const float containerSize = windowData->Metrics.Container;
    const float iconSize = windowData->Metrics.Icon;
    const float selectSize = containerSize;
    const float pad = windowData->Metrics.Pad;
    const float padSelect = (containerSize - selectSize) * 0.5f;
    const float padIcon = (containerSize - iconSize) * 0.5f;
    const float digitHeight = windowData->Metrics.DigitBoxHeight * 0.75f;
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
        const uint32_t selIdx = (uint32_t)windowData->Selection;
        const uint32_t mouseSelIdx = (uint32_t)windowData->MouseSelection;

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

    for (uint32_t i = 0; i < windowData->WinGroups.Size; i++) {
        const SWinGroup* pWinGroup = &windowData->WinGroups.Data[i];

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
        if (windowData->StaticData->Config->AppSwitcherMode == AppSwitcherModeApp && pWinGroup->WindowCount > 1) {
            WCHAR str[] = L"\0\0";
            const uint32_t winCount = min(pWinGroup->WindowCount, 99);
            const uint32_t digitsCount = winCount > 9 ? 2 : 1;
            const float w = windowData->Metrics.DigitBoxHeight;
            const float h = windowData->Metrics.DigitBoxHeight;
            const float p = windowData->Metrics.DigitBoxPad;
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

            const int o = swprintf_s(str, sizeof(str) / sizeof(str[0]), L"%i", winCount);
            ASSERT(o > 0);

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
        const bool selected = i == (uint32_t)windowData->Selection;
        const bool mouseSelected = i == (uint32_t)windowData->MouseSelection;

        if (((selected || mouseSelected) && windowData->StaticData->Config->DisplayName == DisplayNameSel) || windowData->StaticData->Config->DisplayName == DisplayNameAll) {
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
        const uint32_t mouseSelIdx = (uint32_t)windowData->MouseSelection;
        float r0[4];
        CloseButtonRect(r0, &windowData->Metrics, mouseSelIdx);
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
        if (windowData->CloseHover) {
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

    BitBlt(GetDC(hwnd),
        clientRect.left,
        clientRect.top,
        clientRect.right - clientRect.left,
        clientRect.bottom - clientRect.top,
        windowData->DC, 0, 0, SRCCOPY);

    // Always restore old bitmap (see fn doc)
    SelectObject(windowData->DC, oldBitmap);

    // Delete res.
    GdipDeleteFont(fontName);
    GdipDeleteFontFamily(pFontFamily);

    GdipDeleteGraphics(pGraphics);
}

static void Apply(void* data)
{
    struct WindowData* wd = (struct WindowData*)data;
    ApplySwitchApp(&wd->WinGroups.Data[wd->Selection], wd->StaticData->Config->RestoreMinimizedWindows);
}

static void ApplyMouse(void* data)
{
    struct WindowData* wd = (struct WindowData*)data;
    ApplySwitchApp(&wd->WinGroups.Data[wd->MouseSelection], wd->StaticData->Config->RestoreMinimizedWindows);
}

static void MoveSelection(struct WindowData* windowData, int x)
{
    ASSERT(windowData);
    ASSERT(windowData->StaticData);
    ASSERT(windowData->StaticData->Config);
    windowData->Selection = Modulo(windowData->Selection + x, (int)windowData->WinGroups.Size);
    if (!windowData->StaticData->Config->DebugDisableIconFocus)
        SetFocus(windowData->FocusWindows[windowData->Selection]);
    InvalidateRect(windowData->MainWin, 0, FALSE);
    UpdateWindow(windowData->MainWin);
}

static int ProcessKeys(struct WindowData* windowData, UINT uMsg, WPARAM wParam)
{
    switch (uMsg) {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        ASSERT(windowData)
        ASSERT(windowData->StaticData)
        ASSERT(windowData->StaticData->Config)
        if (wParam == VK_ESCAPE) {
            DestroyWin(&windowData->MainWin);
            ClearWinGroupArr(&windowData->WinGroups);
            return 0;
        }
        int x = 0;
        if (
            wParam == windowData->StaticData->Config->Key.AppSwitch
            || wParam == 'L'
            || wParam == 'J'
            || wParam == VK_RIGHT
            || wParam == VK_DOWN) {
            x = 1;
        } else if (
            wParam == windowData->StaticData->Config->Key.PrevApp
            || wParam == 'H'
            || wParam == 'K'
            || wParam == VK_LEFT
            || wParam == VK_UP) {
            x = -1;
        }
        if (x != 0) {
            const bool invert = GetAsyncKeyState((SHORT)windowData->StaticData->Config->Key.Invert) & 0x8000;
            MoveSelection(windowData, invert ? -x : x);
            return 0;
        }
    }
    default:
        break;
    }
    return 1;
}

static LRESULT FocusWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WindowData* appData = NULL;

    if (ProcessKeys(appData, uMsg, wParam) == 0)
        return 0;

    switch (uMsg) {
    case WM_CREATE:
        appData = (struct WindowData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
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

static void DeinitApp(struct WindowData* windowData)
{
#ifdef ASYNC_APPLY
    ASSERT(windowData);
    ASSERT(windowData->StaticData);
    ASSERT(windowData->StaticData->Instance);
    if (!windowData->StaticData->Instance)
        return;
    ApplyWithTimeout(Apply, windowData, windowData->StaticData->Instance);
#else
    ApplySwitchApp(&appData->WinGroups.Data[appData->Selection]);
#endif
}

static void Init(struct WindowData* windowData)
{
    ASSERT(windowData->StaticData);
    ASSERT(windowData->StaticData->Config);
    // Save persistant data and kill child window if any.
    struct StaticData* sd = windowData->StaticData;
    HWND w = windowData->MainWin;
    for (int i = 0; i < windowData->FocusWindowCount; i++) {
        if (!IsRunWindow(windowData->FocusWindows[i]))
            continue;
        DestroyWindow(windowData->FocusWindows[i]);
    }

    if (windowData->DC)
        DeleteDC(windowData->DC);
    if (windowData->Bitmap)
        DeleteObject(windowData->Bitmap);

    // Clear
    *windowData = (struct WindowData) {};

    windowData->StaticData = sd;
    windowData->MainWin = w;
    SWinGroupArr* pWinGroups = &(windowData->WinGroups);
    ASSERT(pWinGroups);
    pWinGroups->Size = 0;
    // Get mouse monitor once if filtering by monitor is enabled
    if (windowData->StaticData->Config->AppFilterMode == AppFilterModeMouseMonitor) {
        POINT mousePos;
        if (GetCursorPos(&mousePos)) {
            windowData->MouseMonitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTONEAREST);
        } else {
            // Fall back to primary monitor if GetCursorPos fails
            windowData->MouseMonitor = MonitorFromPoint((POINT) { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        }
    } else {
        windowData->MouseMonitor = NULL; // Explicitly set NULL when not filtering by monitor
    }
    EnumWindows(FillWinGroups, (LPARAM)windowData);

    const bool invert = GetAsyncKeyState((SHORT)windowData->StaticData->Config->Key.Invert) & 0x8000;
    windowData->Selection = Modulo(windowData->Selection + (invert ? -1 : 1), (int)windowData->WinGroups.Size);
    windowData->MouseSelection = 0;

    if (windowData->WinGroups.Size == 0)
        return;

    ComputeMetrics(windowData->WinGroups.Size,
        windowData->StaticData->Config->Scale,
        &windowData->Metrics,
        windowData->StaticData->Config->MultipleMonitorMode == MultipleMonitorModeMouse);

    // Needed for exact client area.
    RECT r = {
        (LONG)windowData->Metrics.WinPosX,
        (LONG)windowData->Metrics.WinPosY,
        (LONG)(windowData->Metrics.WinPosX + windowData->Metrics.WinX),
        (LONG)(windowData->Metrics.WinPosY + windowData->Metrics.WinY)
    };
    AdjustWindowRect(&r, (DWORD)GetWindowLong(windowData->MainWin, GWL_STYLE), false);
    SetWindowPos(windowData->MainWin, 0, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);

    // Rounded corners for Win 11
    // Values are from cpp enums DWMWINDOWATTRIBUTE and DWM_WINDOW_CORNER_PREFERENCE
    const uint32_t rounded = 2;
    DwmSetWindowAttribute(windowData->MainWin, 33, &rounded, sizeof(rounded));

    {
        RECT clientRect;
        ASSERT(GetWindowRect(windowData->MainWin, &clientRect));

        HDC winDC = GetDC(windowData->MainWin);
        ASSERT(winDC);
        windowData->DC = CreateCompatibleDC(winDC);
        ASSERT(windowData->DC != NULL);
        windowData->Bitmap = CreateCompatibleBitmap(
            winDC,
            clientRect.right - clientRect.left,
            clientRect.bottom - clientRect.top);
        ASSERT(windowData->Bitmap != NULL);
        ReleaseDC(windowData->MainWin, winDC);
    }

    InvalidateRect(windowData->MainWin, NULL, FALSE);
    UpdateWindow(windowData->MainWin);
    const DWORD ret = SetForegroundWindow(windowData->MainWin);
    (void)ret;

    SetLayeredWindowAttributes(windowData->MainWin, 0, 0, LWA_ALPHA);
    ShowWindow(windowData->MainWin, SW_SHOW);
    RECT clientRect = { 0, 0, (LONG)windowData->Metrics.WinX, (LONG)windowData->Metrics.WinY };
    Draw(windowData, GetDC(windowData->MainWin), clientRect);
    SetLayeredWindowAttributes(windowData->MainWin, 0, 255, LWA_ALPHA);

    SetCapture(windowData->MainWin);

    if (!windowData->StaticData->Config->DebugDisableIconFocus) {
        for (int i = 0; i < windowData->WinGroups.Size; i++) {
            const int iconContainerSize = (int)windowData->Metrics.Container;
            const int pad = (int)windowData->Metrics.Pad;
            int x = pad + (i * iconContainerSize);
            windowData->FocusWindows[i] = CreateWindowEx(0, FOCUS_CLASS_NAME, NULL,
                WS_CHILD /* | WS_VISIBLE */,
                x, pad, iconContainerSize, iconContainerSize,
                windowData->MainWin, NULL, windowData->StaticData->Instance, windowData);
            windowData->FocusWindowCount++;
        }
    }
}

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WindowData windowData = {};

    if (ProcessKeys(&windowData, uMsg, wParam) == 0)
        return 0;

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        ASSERT(windowData.StaticData);
        ASSERT(windowData.StaticData->Config);
        // Otherwise cursor is busy on hover. I don't understand why.
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        if (!windowData.StaticData->Config->Mouse)
            return 0;
        const int iconContainerSize = (int)windowData.Metrics.Container;
        const int pad = (int)windowData.Metrics.Pad;
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        windowData.MouseSelection = min(max(0, (x - pad) / iconContainerSize), (int)windowData.WinGroups.Size - 1);
        float r[4];
        CloseButtonRect(r, &windowData.Metrics, windowData.MouseSelection);
        windowData.CloseHover = x < (int)r[2] && x > (int)r[0] && y > (int)r[1] && y < (int)r[3];
        InvalidateRect(windowData.MainWin, 0, FALSE);
        UpdateWindow(windowData.MainWin);
        return 0;
    }
    case WM_CREATE: {
        windowData = (struct WindowData) {};
        windowData.StaticData = (struct StaticData*)((CREATESTRUCTA*)lParam)->lpCreateParams;
        windowData.MainWin = hwnd;
        Init(&windowData);
        return 0;
    }
    case MSG_FOCUS: {
        // uia set focus here gives inconsistent app behavior IDK why.
        // UIASetFocus(focusWindows[appData->Selection]);
        ASSERT(windowData.StaticData);
        ASSERT(windowData.StaticData->Config);
        if (!windowData.StaticData->Config->DebugDisableIconFocus) {
            SetFocus(windowData.FocusWindows[windowData.Selection]);
            return 0;
        }
        break;
    }
    case MSG_REFRESH: {
        ClearWinGroupArr(&windowData.WinGroups);
        Init(&windowData);
        return 0;
    }
    case WM_LBUTTONUP: {
        ASSERT(windowData.StaticData);
        ASSERT(windowData.StaticData->Config);
        if (!IsInside(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), hwnd)) {
            DestroyWin(&windowData.MainWin);
            ClearWinGroupArr(&windowData.WinGroups);
            return 0;
        }
        if (!windowData.StaticData->Config->Mouse)
            return 0;

        if (windowData.CloseHover) {
            static HANDLE ht = 0;
            static CloseThreadData ctd = {};
            if (ht)
                TerminateThread(ht, 0);
            ctd.Count = 0;

            ctd.MainWin = hwnd;
            const SWinGroup* winGroup = &windowData.WinGroups.Data[windowData.MouseSelection];
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
        ApplyWithTimeout(ApplyMouse, &windowData, windowData.StaticData->Instance);
#else
        ApplySwitchApp(&appData->WinGroups.Data[appData->MouseSelection]);
#endif
        windowData.Selection = 0;
        windowData.MouseSelection = 0;
        windowData.CloseHover = false;
        DestroyWin(&windowData.MainWin);
        ClearWinGroupArr(&windowData.WinGroups);
        return 0;
    }
    case WM_CLOSE: {
        DeinitApp(&windowData);
        DestroyWin(&hwnd);
        return 0;
    }
    case WM_DESTROY: {

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

        windowData.Selection = 0;
        ClearWinGroupArr(&windowData.WinGroups);
        DeleteDC(windowData.DC);
        DeleteObject(windowData.Bitmap);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        if (BeginPaint(hwnd, &ps) == NULL) {
            ASSERT(false);
            return 0;
        }
        RECT clientRect;
        ASSERT(GetClientRect(hwnd, &clientRect));
        Draw(&windowData, ps.hdc, clientRect);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
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