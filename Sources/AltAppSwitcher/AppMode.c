#define COBJMACROS
#include "AppMode.h"
#include <stdio.h>
#include <stdlib.h>
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
#include <limits.h>
// https://stackoverflow.com/questions/71437203/proper-way-of-activating-a-window-using-winapi
#include <uiautomationclient.h>
#include <gdiplus/gdiplusenums.h>
#include <PropKey.h>
#include <Shobjidl.h>
#include <shlobj.h>
#include <math.h>
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
    uint32_t ColCount;
    float Container;
    float Icon;
    float Pad;
    float AppNameHeight;
    float DigitBoxHeight;
    float DigitBoxPad;
    float PathThickness;
    float TileAndNameHeight;
} Metrics;

typedef enum Mode {
    ModeNone,
    ModeApp,
    ModeWin
} Mode;

typedef struct SUWPIconMapElement {
    wchar_t UserModelID[512];
    GpBitmap* Icon;
    wchar_t AppName[MAX_PATH];
    uint32_t UseIdx;
} SUWPIconMapElement;

#define UWPICONMAPSIZE MAX_WIN_GROUPS
struct UWPIconMap {
    SUWPIconMapElement Data[UWPICONMAPSIZE];
    uint32_t UseIdx;
};

struct StaticData {
    struct GraphicsResources GraphicsResources;
    struct UWPIconMap UWPIconMap;
    bool Elevated;
    const struct Config* Config;
    const struct KeyConfig* KeyConfigScanCodes;
    HMODULE Instance;
    ULONG_PTR GdiplusToken;
    IVirtualDesktopManager* VDM;
};

static struct StaticData StaticData = { };

struct WindowData {
    HWND MainWin;
    int Selection;
    int MouseSelection; // -1 if no tile is hovered
    int LastSelection;
    bool CloseHover;
    POINT InitCursorPos;
    bool MouseMoved;
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

void CloseAppGroup(const SWinGroup* winGroup, HWND hwnd);

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
    PROPVARIANT pv = { };
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

static void FindActualPID(HWND hwnd, DWORD* PID)
{
    static char className[512];
    GetClassName(hwnd, className, 512);
#if false
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
#endif

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

static bool GetAppInfoFromMap(struct UWPIconMap* map, const wchar_t* aumid, GpBitmap** outIcon, wchar_t* outAppName)
{
    for (uint32_t i = 0; i < UWPICONMAPSIZE; i++) {
        if (wcscmp(map->Data[i].UserModelID, aumid) != 0)
            continue;
        *outIcon = map->Data[i].Icon;
        wcscpy(outAppName, map->Data[i].AppName);
        map->Data[i].UseIdx = map->UseIdx;
        return true;
    }
    return false;
}

static void StoreAppInfoToMap(struct UWPIconMap* map, const wchar_t* aumid, GpBitmap* icon, const wchar_t* appName)
{
    uint32_t storeIdx = 0xffffffff;
    uint32_t maxDelta = 0;
    for (uint32_t i = 0; i < UWPICONMAPSIZE; i++) {
        if (map->Data[i].UserModelID[0] == L'\0') {
            storeIdx = i;
            break;
        }
        // Track oldest
        {
            if (map->Data[i].UseIdx > map->UseIdx) {
                storeIdx = i;
                break;
            }
            uint32_t delta = map->UseIdx - map->Data[i].UseIdx;
            if (delta > maxDelta) {
                maxDelta = delta;
                storeIdx = i;
            }
        }
    }
    // No free slot, all slots has been used this run.
    // Should not happens since MAX_WIN_GROUPS == UWPICONMAPSIZE
    if (storeIdx == 0xffffffff) {
        ASSERT(false);
        return;
    }

    if (map->Data[storeIdx].UserModelID[0] != L'\0') {
        GdipDisposeImage(map->Data[storeIdx].Icon);
        map->Data[storeIdx] = (SUWPIconMapElement) { };
    }

    wcscpy(map->Data[storeIdx].UserModelID, aumid);
    wcscpy(map->Data[storeIdx].AppName, appName);
    map->Data[storeIdx].Icon = icon;
    map->Data[storeIdx].UseIdx = map->UseIdx;
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

struct GetIconGroupNameData {
    char* outName;
    int inIdx;
};

static BOOL GetIconGroupName(HMODULE hModule, LPCSTR lpType, LPSTR lpName, LONG_PTR lParam)
{
    (void)hModule;
    (void)lpType;
    (void)lpName;
    (void)lParam;
    struct GetIconGroupNameData* data = (struct GetIconGroupNameData*)lParam;
    if (data->inIdx < 0) {
        // Negative value means resource ID.
        // Testable with "run" window.
        // Positive means icon index. Not handled, needs a repro.
        if (IS_INTRESOURCE(lpName) && ((WORD)(ULONG_PTR)lpName == -data->inIdx)) {
            data->outName = lpName;
            return false; // Found
        }
        return true;
    }
    // No resource specified: pick first icon group.
    if (IS_INTRESOURCE(lpName)) {
        data->outName = lpName;
    } else {
        strcpy_s(data->outName, sizeof(char) * 256, lpName);
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

static GpBitmap* GetIconFromBinary(const wchar_t* binPath, int iconIdx)
{
    HMODULE module = LoadLibraryExW(binPath, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!module) {
        static char binPathChar[512];
        wcharToChar(binPathChar, binPath);
        VERIFY_MSG(false, "LoadLibrary failed with path: %s", binPathChar);
        return NULL;
    }

    // Finds icon resource in module
    uint32_t iconResID = 0xFFFFFFFF;
    {
        char name[256];
        // char* indirection is mandatory as EnumResourceNames might write the pointer
        // and not the target buffer.
        struct GetIconGroupNameData data = { .outName = name, .inIdx = iconIdx };
        EnumResourceNames(module, RT_GROUP_ICON, GetIconGroupName, (LONG_PTR)&data);
        HRSRC iconGrp = FindResource(module, data.outName, RT_GROUP_ICON);
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
        BITMAP bm = { };
        GetObject(hbm, sizeof(BITMAP), &bm);
        const int iconSize = bm.bmWidth;
        GdipCreateBitmapFromScan0(iconSize, iconSize, 4 * iconSize, PixelFormat32bppARGB, NULL, &out);
        GpRect r = { 0, 0, iconSize, iconSize };
        BitmapData dstData = { };
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
            BITMAP bitmapMask = { };
            GetObject(hbmMask, sizeof(bitmapMask), (LPVOID)&bitmapMask);
            unsigned int maskByteSize = bitmapMask.bmWidthBytes * bitmapMask.bmHeight;
            static char maskData[256 * 256 * 1 / 8] = { };
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
        WINDOWINFO wi = { };
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(hwnd, &wi);
        if (wi.atomWindowType != 0x8002)
            return false;
    }

    const HWND owner = GetAncestor(hwnd, GA_ROOTOWNER);
    if (owner == NULL)
        return false;

    {
        WINDOWINFO wi = { };
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(owner, &wi);
        if (wi.atomWindowType != 0xC01A)
            return false;
    }

    return true;
}

// Copies a 32bpp shell image to a GDI+ bitmap.
static GpBitmap* ShellImageToGpBitmap(HBITMAP hbm)
{
    BITMAP bm = { };
    GetObject(hbm, sizeof(BITMAP), &bm);
    const uint32_t pixelCount = (uint32_t)(bm.bmWidth * bm.bmHeight);
    uint32_t* pixels = (uint32_t*)malloc(sizeof(uint32_t) * pixelCount);
    if (pixels == NULL)
        return NULL;
    BITMAPINFO bi = { };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight; // Top-down, as GDI+
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    GetDIBits(dc, hbm, 0, (UINT)bm.bmHeight, pixels, &bi, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);

    // Images from packaged apps are premultiplied, images from icons
    // are not. Premultiplied colors cannot exceed alpha.
    bool premultiplied = true;
    for (uint32_t i = 0; i < pixelCount && premultiplied; i++) {
        const uint32_t a = pixels[i] >> 24u;
        premultiplied = ((pixels[i] >> 16u) & 0xFFu) <= a && ((pixels[i] >> 8u) & 0xFFu) <= a && (pixels[i] & 0xFFu) <= a;
    }
    const PixelFormat format = premultiplied ? PixelFormat32bppPARGB : PixelFormat32bppARGB;

    GpBitmap* out = NULL;
    GdipCreateBitmapFromScan0(bm.bmWidth, bm.bmHeight, 0, format, NULL, &out);
    GpRect r = { 0, 0, bm.bmWidth, bm.bmHeight };
    BitmapData dstData = { };
    GdipBitmapLockBits(out, &r, ImageLockModeWrite, format, &dstData);
    memcpy_s(dstData.Scan0, sizeof(uint32_t) * pixelCount, pixels, sizeof(uint32_t) * pixelCount);
    GdipBitmapUnlockBits(out, &dstData);
    free(pixels);
    return out;
}

// Gets icon and name of a packaged app (or an app registered with an explicit
// AUMID) from the shell's apps folder, as the start menu and taskbar do. It is
// backed by the system icon cache.
static bool GetAppInfoFromShell(const wchar_t* aumid, GpBitmap** outIcon, wchar_t* outAppName)
{
    IShellItem* item = NULL;
    HRESULT hr = SHCreateItemInKnownFolder(&FOLDERID_AppsFolder, 0, aumid, &IID_IShellItem, (void**)&item);
    if (FAILED(hr))
        return false;
    wchar_t* name = NULL;
    hr = IShellItem_GetDisplayName(item, SIGDN_NORMALDISPLAY, &name);
    if (FAILED(hr)) {
        IShellItem_Release(item);
        return false;
    }
    wcsncpy(outAppName, name, MAX_PATH - 1);
    outAppName[MAX_PATH - 1] = L'\0';
    CoTaskMemFree(name);

    IShellItemImageFactory* factory = NULL;
    hr = IShellItem_QueryInterface(item, &IID_IShellItemImageFactory, (void**)&factory);
    IShellItem_Release(item);
    if (FAILED(hr))
        return false;
    HBITMAP hbm = NULL;
    const SIZE size = { 256, 256 };
    const SIIGBF flags = (SIIGBF)((unsigned int)SIIGBF_ICONONLY | (unsigned int)SIIGBF_BIGGERSIZEOK);
    hr = IShellItemImageFactory_GetImage(factory, size, flags, &hbm);
    IShellItemImageFactory_Release(factory);
    if (FAILED(hr) || hbm == NULL)
        return false;
    *outIcon = ShellImageToGpBitmap(hbm);
    DeleteObject(hbm);
    return true;
}

static GpBitmap* BitmapToGpBitmap(HBITMAP hbm)
{
    // Creates a gdi bitmap from the win base api bitmap
    GpBitmap* out;
    {
        BITMAP bm = { };
        GetObject(hbm, sizeof(BITMAP), &bm);
        const uint32_t iconSize = bm.bmWidth;
        GdipCreateBitmapFromScan0((int)iconSize, (int)iconSize, (int)(4 * iconSize), PixelFormat32bppARGB, NULL, &out);
        GpRect r = { 0, 0, (int)iconSize, (int)iconSize };
        BitmapData dstData = { };
        GdipBitmapLockBits(out, &r, 0, PixelFormat32bppARGB, &dstData);
        GetBitmapBits(hbm, (LONG)(sizeof(uint32_t) * iconSize * iconSize), dstData.Scan0);
        GdipBitmapUnlockBits(out, &dstData);
    }
    return out;
}

static bool GettingAppInfos = false;

void GetAppInfos(DWORD PID, struct StaticData* staticData, const wchar_t* aumid, GpBitmap** outIcon, wchar_t* outAppName)
{
    ASSERT(*outIcon == NULL);
    ASSERT(outAppName[0] == L'\0');
    bool found = GetAppInfoFromMap(&staticData->UWPIconMap, aumid, outIcon, outAppName);
    if (found)
        return;
    GettingAppInfos = true;

    // Win32 apps without explicit AUMID are identified by their exe path, see GetProcessAUMI.
    if (wcschr(aumid, L'\\') == NULL)
        found = GetAppInfoFromShell(aumid, outIcon, outAppName);

    // From exe
    if (!found) {
        outAppName[0] = L'\0';
        const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, PID);
        static wchar_t exePath[512] = { };
        exePath[0] = L'\0';
        GetModuleFileNameExW(process, NULL, exePath, 512);
        CloseHandle(process);
        if (exePath[0] != L'\0') {
            GetAppName(exePath, outAppName);
            *outIcon = GetIconFromBinary(exePath, 0);
        }
    }

    // Default icon
    if (*outIcon == NULL) {
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
        GpBitmap* out = BitmapToGpBitmap(hbm);

        DeleteObject(hbm);
        *outIcon = out;
    }

    StoreAppInfoToMap(&staticData->UWPIconMap, aumid, *outIcon, outAppName);
    GettingAppInfos = false;
}

static BOOL WarmCache(HWND hwnd, LPARAM lParam)
{
    struct StaticData* staticData = (struct StaticData*)lParam;
    if (!IsEligibleWindow(hwnd, staticData->Config, NULL, false, staticData->VDM))
        return true;
    DWORD PID = 0;
    FindActualPID(hwnd, &PID);
    static wchar_t AUMID[512];
    {
        BOOL found = GetWindowAUMI(hwnd, AUMID);
        if (!found)
            GetProcessAUMI(PID, AUMID);
    }
    GpBitmap* appIcon = NULL;
    static wchar_t appName[MAX_PATH];
    appName[0] = L'\0';
    GetAppInfos(PID, staticData, AUMID, &appIcon, appName);
    return true;
}

// Resolve app infos as soon as a window is shown, uncloaked (UWP) or activated,
// so apps launched after startup are not resolved when the switcher is invoked.
static HWINEVENTHOOK WarmCacheHooks[3] = { };

#ifndef EVENT_OBJECT_UNCLOAKED
#define EVENT_OBJECT_UNCLOAKED 0x8018 // Win 8+, guarded by _WIN32_WINNT in winuser.h
#endif

static void CALLBACK WarmCacheWinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD idEventTime)
{
    (void)hook;
    (void)event;
    (void)idEventThread;
    (void)idEventTime;
    // COM calls made while resolving app infos can pump messages and
    // dispatch win events: don't reenter.
    static bool busy = false;
    if (busy || GettingAppInfos)
        return;
    if (!hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
        return;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd)
        return;
    busy = true;
    WarmCache(hwnd, (LPARAM)&StaticData);
    busy = false;
}

static BOOL FillWinGroups(HWND hwnd, LPARAM lParam)
{
    struct WindowData* windowData = (struct WindowData*)lParam;

    if (!IsEligibleWindow(hwnd, windowData->StaticData->Config, windowData->MouseMonitor, false, windowData->StaticData->VDM))
        return true;

    DWORD PID = 0;

    FindActualPID(hwnd, &PID);
    static wchar_t AUMID[512];
    {
        BOOL found = GetWindowAUMI(hwnd, AUMID);
        if (!found)
            GetProcessAUMI(PID, AUMID);
    }

    ATOM winClass = IsRunWindow(hwnd) ? 0x8002 : 0; // Run

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
        // Bypass if elevation != self
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
            CloseHandle(process);
            if (elevated && !windowData->StaticData->Elevated)
                return true;
        }

        group = &winAppGroupArr->Data[winAppGroupArr->Size++];
        wcscpy_s(group->AUMID, sizeof(group->AUMID), AUMID);
        group->WinClass = winClass;
        ASSERT(group->WindowCount == 0);

        // Icon
        ASSERT(group->IconBitmap == NULL);

        GetAppInfos(PID, windowData->StaticData, group->AUMID, &group->IconBitmap, group->AppName);
        {
            group->Caption[0] = L'\0';
            GetWindowTextW(hwnd, group->Caption, MAX_PATH);
        }
    }

    group->Windows[group->WindowCount++] = hwnd;
    return true;
}

static int Clamp(int x, int y, int z)
{
    return max(min(x, z), y);
}

static uint32_t PosToIdx(const Metrics* m, int x, int y, int count)
{
    x = Clamp(x - (int)m->Pad, 0, (int)m->Container * (int)m->ColCount);
    int i = ((x - (int)m->Pad) / (int)m->Container) % (int)m->ColCount;
    i += ((y - (int)m->Pad) / (int)(m->TileAndNameHeight)) * (int)m->ColCount;
    return (uint32_t)min(max(0, i), (int)(count - 1));
}
// Index of the tile (icon and name) under x, y, or -1.
static int TileAtPos(const Metrics* m, int x, int y, uint32_t count)
{
    if (x < (int)m->Pad || y < (int)m->Pad)
        return -1;
    const int col = (x - (int)m->Pad) / (int)m->Container;
    const int row = (y - (int)m->Pad) / (int)m->TileAndNameHeight;
    if (col >= (int)m->ColCount)
        return -1;
    const int idx = (row * (int)m->ColCount) + col;
    return idx < (int)count ? idx : -1;
}

static void IdxToPos(const Metrics* m, int idx, int* outX, int* outY)
{
    *outX = (int)m->Pad + (int)m->Container * (idx % (int)m->ColCount);
    *outY = (int)m->Pad + (int)(m->TileAndNameHeight) * (idx / (int)m->ColCount);
}

static void ComputeMetrics(uint32_t iconCount, Metrics* metrics, const struct Config* cfg)
{
    int monitorOffset[2] = { 0, 0 };
    int monitorSize[2] = { 0, 0 };

    POINT mousePos = { 0, 0 };
    if (cfg->MultipleMonitorMode == MultipleMonitorModeMouse)
        GetCursorPos(&mousePos);
    HMONITOR monitor = MonitorFromPoint(mousePos, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &info);
    monitorOffset[0] = info.rcMonitor.left;
    monitorOffset[1] = info.rcMonitor.top;
    monitorSize[0] = info.rcMonitor.right - info.rcMonitor.left;
    monitorSize[1] = info.rcMonitor.bottom - info.rcMonitor.top;

    uint32_t dimX = (cfg->IconsPerRow > 0) && ((int)iconCount > cfg->IconsPerRow) ? cfg->IconsPerRow : iconCount;
    uint32_t dimY = ((iconCount + dimX - 1) / dimX);
    float scale = max(cfg->Scale, 0.5f);
    const int centerY = monitorSize[1] / 2;
    const int centerX = monitorSize[0] / 2;
    const int screenWidth = monitorSize[0];
    const float containerRatio = 1.25f;
    float iconSize = (float)GetSystemMetrics(SM_CXICON) * scale;
    const float appNameHeightRatio = max(0.25 * iconSize, 16.0f) / iconSize; // Keep room for app name
    const float padRatio = max(0.25, appNameHeightRatio);
    const int sizeX = min(iconSize * ((int)dimX * containerRatio + 2.0f * padRatio), screenWidth * 0.9);
    iconSize = ((float)sizeX / ((((float)dimX * containerRatio) + (2.0f * padRatio))));
    const uint32_t halfSizeX = sizeX / 2;
    uint32_t sizeY = 0
        + dimY * (uint32_t)(iconSize * containerRatio) // tiles
        + (uint32_t)(padRatio * iconSize); // top win padding
    if (cfg->DisplayName == DisplayNameNone)
        sizeY += (uint32_t)(padRatio * iconSize); // bot win padding
    else
        sizeY += (uint32_t)((padRatio - appNameHeightRatio) * iconSize); // bot padding: only if bigger than app name zone
    if (cfg->DisplayName == DisplayNameAll || cfg->DisplayName == DisplayNameSel)
        sizeY += dimY * (uint32_t)(iconSize * appNameHeightRatio); // names
    else if (cfg->DisplayName == DisplayNameSelFull)
        sizeY += (uint32_t)(iconSize * appNameHeightRatio); // name

    const uint32_t halfSizeY = sizeY / 2;
    metrics->ColCount = dimX;
    metrics->WinPosX = centerX - halfSizeX + monitorOffset[0];
    metrics->WinPosY = centerY - halfSizeY + monitorOffset[1];
    metrics->WinX = sizeX;
    metrics->WinY = sizeY;
    metrics->Icon = iconSize;
    metrics->Container = iconSize * containerRatio;
    metrics->Pad = iconSize * padRatio;
    metrics->AppNameHeight = iconSize * appNameHeightRatio;
    metrics->DigitBoxHeight = min(max(metrics->Container * 0.15f, 16.0f), metrics->Container * 0.5f); // Min size of 16 for text
    metrics->PathThickness = 2.0f;
    metrics->DigitBoxPad = (0.15f * metrics->DigitBoxHeight) + metrics->PathThickness;
    metrics->TileAndNameHeight = metrics->Container
        + (cfg->DisplayName == DisplayNameAll || cfg->DisplayName == DisplayNameSel ? metrics->AppNameHeight : 0.0f);
}

static const char MAIN_CLASS_NAME[] = "AltAppSwitcher";
static const char FOCUS_CLASS_NAME[] = "AASFocus";

static void DestroyWin(HWND* win)
{
    DestroyWindow(*win);
    *win = NULL;
}

static volatile LONG SelfClosed = 0;

// Closes the switcher on its own input (mouse click, escape). The keyboard hook
// otherwise only learns the switcher is closed when the hold key is released.
static void SelfClose(HWND* win)
{
    DestroyWin(win);
    InterlockedExchange(&SelfClosed, 1);
}

bool AppModeTakeSelfClosed(void)
{
    return InterlockedExchange(&SelfClosed, 0) != 0;
}

static void Draw(struct WindowData* windowData, RECT clientRect);

static LRESULT FocusWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void AppModeRefreshGraphicsResources()
{
    DeinitGraphicsResources(&StaticData.GraphicsResources);
    InitGraphicsResources(&StaticData.GraphicsResources, StaticData.Config);
}

void AppModeInit(HINSTANCE instance, const struct Config* cfg, IVirtualDesktopManager* VDM)
{
    StaticData.Instance = instance;
    StaticData.Config = cfg;
    StaticData.Elevated = false;
    StaticData.VDM = VDM;
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
        GdiplusStartupInput gdiplusStartupInput = { };
        gdiplusStartupInput.GdiplusVersion = 1;
        uint32_t status = GdiplusStartup(&StaticData.GdiplusToken, &gdiplusStartupInput, NULL);
        ASSERT(!status);
    }

    InitGraphicsResources(&StaticData.GraphicsResources, cfg);

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc = MainWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = MAIN_CLASS_NAME;
        wc.cbWndExtra = sizeof(void*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClass(&wc);
    }

    {
        WNDCLASS wc = { };
        wc.lpfnWndProc = FocusWindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = FOCUS_CLASS_NAME;
        wc.cbWndExtra = sizeof(void*);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.hbrBackground = GetSysColorBrush(COLOR_HIGHLIGHT);
        RegisterClass(&wc);
    }

    EnumWindows(WarmCache, (LPARAM)&StaticData);
    WarmCacheHooks[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL,
        WarmCacheWinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    WarmCacheHooks[1] = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL,
        WarmCacheWinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    WarmCacheHooks[2] = SetWinEventHook(EVENT_OBJECT_UNCLOAKED, EVENT_OBJECT_UNCLOAKED, NULL,
        WarmCacheWinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void AppModeDeinit()
{
    for (uint32_t i = 0; i < sizeof(WarmCacheHooks) / sizeof(WarmCacheHooks[0]); i++) {
        if (WarmCacheHooks[i])
            UnhookWinEvent(WarmCacheHooks[i]);
        WarmCacheHooks[i] = NULL;
    }
    for (uint32_t i = 0; i < UWPICONMAPSIZE; i++) {
        if (StaticData.UWPIconMap.Data[i].Icon)
            GdipDisposeImage(StaticData.UWPIconMap.Data[i].Icon);
    }
    StaticData.UWPIconMap = (struct UWPIconMap) { };
    DeinitGraphicsResources(&StaticData.GraphicsResources);
    GdiplusShutdown(StaticData.GdiplusToken);
    UnregisterClass(MAIN_CLASS_NAME, StaticData.Instance);
    UnregisterClass(FOCUS_CLASS_NAME, StaticData.Instance);
}

HWND AppModeCreateWindow()
{
    AAS_MSG("AppModeCreateWindow");
    DWORD curThread = GetCurrentThreadId();
    DWORD fgwinthread = TryAttachToForeground();
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_NOACTIVATE, // Optional window styles (WS_EX_)
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
    if (fgwinthread && fgwinthread != curThread) {
        WINBOOL r = AttachThreadInput(GetCurrentThreadId(), fgwinthread, FALSE);
        VERIFY(r);
    }
    AAS_MSG("AppModeCreateWindow End");
    return hwnd;
}

void AppModeDestroyWindow(HWND window)
{
    SendMessage(window, WM_CLOSE, (WPARAM)0, (LPARAM)0);
}

static void ClearWinGroupArr(SWinGroupArr* winGroups)
{
    for (uint32_t i = 0; i < winGroups->Size; i++) {
        winGroups->Data[i].IconBitmap = NULL;
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

    {
        // https://github.com/hdlx/AltAppSwitcher/issues/96
        SetForegroundWindow(winGroup->Windows[0]);
    }

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
    RECT r = { };
    ASSERT(GetClientRect(win, &r));
    return x > r.left && x < r.right && y > r.top && y < r.bottom;
}

static void CloseButtonRect(float* outRect, const Metrics* m, uint32_t idx)
{
    const float w = m->DigitBoxHeight;
    const float p = m->DigitBoxPad;
    int x;
    int y;
    IdxToPos(m, (int)idx, &x, &y);
    RectF r = {
        (float)x + m->Container - w - p,
        (float)y + p,
        w,
        w
    };
    outRect[0] = r.X;
    outRect[1] = r.Y;
    outRect[2] = r.X + r.Width;
    outRect[3] = r.Y + r.Height;
}

static void Draw(struct WindowData* windowData, RECT clientRect)
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
    const float padSelect = (containerSize - selectSize) * 0.5f;
    const float padIcon = (containerSize - iconSize) * 0.5f;
    const float digitHeight = windowData->Metrics.DigitBoxHeight * 0.75f;
    const float nameHeight = windowData->Metrics.AppNameHeight * 0.6f;
    const float namePad = windowData->Metrics.AppNameHeight * 0.2f;

    // Resources
    GpFont* fontName = NULL;
    GpFontFamily* pFontFamily = NULL;
    {
        GpFontCollection* fc = NULL;
        ASSERT(Ok == GdipNewInstalledFontCollection(&fc));
        // if (Ok != GdipCreateFontFamilyFromName(L"Segoe UI Emoji", fc, &pFontFamily)) {
        //     ASSERT(Ok == GdipCreateFontFamilyFromName(L"Segoe UI", fc, &pFontFamily));
        // }
        ASSERT(Ok == GdipCreateFontFamilyFromName(L"Segoe UI", fc, &pFontFamily));
        ASSERT(Ok == GdipCreateFont(pFontFamily, nameHeight, FontStyleRegular, (int)MetafileFrameUnitPixel, &fontName));
    }

    float tree_dots_width = 0.0f;
    {
        RectF rout;
        RectF rin = { 0, 0, 1000.0f, 1000.0f };
        int maxCount = 0;
        GdipMeasureString(pGraphics, L"...", 3, fontName, &rin, pGraphRes->pFormat, &rout, &maxCount, 0);
        tree_dots_width = rout.Width;
    }

    // Selection box
    {
        const uint32_t selIdx = (uint32_t)windowData->Selection;

        if (windowData->MouseSelection >= 0) {
            int tileX = 0;
            int tileY = 0;
            IdxToPos(&windowData->Metrics, windowData->MouseSelection, &tileX, &tileY);
            RectF selRect = { (float)tileX, (float)tileY, selectSize, selectSize };
            DrawRoundedRect(pGraphics, NULL, pGraphRes->pBrushBgHighlight, &selRect, 10);
        }

        {
            int tileX = 0;
            int tileY = 0;
            IdxToPos(&windowData->Metrics, (int)selIdx, &tileX, &tileY);
            RectF selRect = { (float)tileX, (float)tileY, selectSize, selectSize };
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
        int tileX = 0;
        int tileY = 0;
        IdxToPos(&windowData->Metrics, (int)i, &tileX, &tileY);
        float x = (float)tileX;
        float y = (float)tileY;

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
                (x + padSelect + selectSize - p - w), // (x + padSelect + selectSize - p - w - (closeButton ? p + w : 0)),
                (y + padSelect + selectSize - p - w),
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
        const bool mouseSelected = windowData->MouseSelection >= 0 && i == (uint32_t)windowData->MouseSelection;
        const bool latestSelected = i == (uint32_t)windowData->LastSelection;

        if (((selected || mouseSelected) && windowData->StaticData->Config->DisplayName == DisplayNameSel)
            || (latestSelected && windowData->StaticData->Config->DisplayName == DisplayNameSelFull)
            || windowData->StaticData->Config->DisplayName == DisplayNameAll) {
            // https://learn.microsoft.com/en-us/windows/win32/gdiplus/-gdiplus-obtaining-font-metrics-use
            const float h = nameHeight;
            const float py = namePad;
            const float px = namePad * 0.25f;
            const float w = containerSize - (2.0f * px);
            RectF r = {
                (float)(int)(x + px),
                (float)(int)(y + containerSize + py),
                (float)(int)(w),
                (float)(int)(h)
            };
            if (windowData->StaticData->Config->DisplayName == DisplayNameSelFull) {
                r = (RectF) {
                    (float)(int)(0.0f + px),
                    (float)(int)(0.0f + (float)windowData->Metrics.WinY - nameHeight - py),
                    (float)(int)((float)windowData->Metrics.WinX - (px * 2.0f)),
                    (float)(int)(nameHeight)
                };
            }

            static wchar_t name[MAX_PATH];
            const wchar_t* displayName = wcslen(pWinGroup->Caption) > 0 ? pWinGroup->Caption : pWinGroup->AppName;
            int count = (int)wcslen(displayName);
            if (count != 0) {
                RectF rin = { 0, 0, r.Width - tree_dots_width, r.Height };
                RectF rout;
                int maxCount = 0;
                GdipMeasureString(pGraphics, displayName, count, fontName, &rin, pGraphRes->pFormat, &rout, &maxCount, 0);
                wcsncpy(name, displayName, min(maxCount, count));
                if (count > maxCount) {
                    wcscpy(&name[maxCount], L"...");
                    count = maxCount + 3;
                }
                GdipDrawString(pGraphics, name, count, fontName, &r, pGraphRes->pFormat, pGraphRes->pBrushText);
            }
        }
    }

    // Close button
    if (windowData->StaticData->Config->Mouse && windowData->MouseSelection >= 0) {
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
    windowData->LastSelection = windowData->Selection;
    if (windowData->StaticData->Config->MouseKbCommonSel) {
        windowData->MouseSelection = windowData->Selection;
    }
    SetFocus(windowData->FocusWindows[windowData->Selection]);
    InvalidateRect(windowData->MainWin, 0, FALSE);
    UpdateWindow(windowData->MainWin);
}

static int ProcessKeys(struct WindowData* windowData, UINT uMsg, WPARAM wParam, LPARAM lp)
{
    (void)lp;
    switch (uMsg) {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        unsigned int vkCode = wParam;
        UINT scan = (lp >> 16) & 0xFF;
        UINT extended = (lp >> 24) & 1;
        scan = scan | (extended << 8);
        ASSERT(windowData)
        ASSERT(windowData->StaticData)
        ASSERT(windowData->StaticData->Config)
        if (vkCode == VK_ESCAPE) {
            SelfClose(&windowData->MainWin);
            ClearWinGroupArr(&windowData->WinGroups);
            return 0;
        }
        int x = 0;
        if (
            scan == windowData->StaticData->Config->Key.AppSwitch
            || vkCode == 'L'
            || vkCode == 'J'
            || vkCode == VK_RIGHT
            || vkCode == VK_DOWN) {
            x = 1;
        } else if (
            scan == windowData->StaticData->Config->Key.PrevApp
            || vkCode == 'H'
            || vkCode == 'K'
            || vkCode == VK_LEFT
            || vkCode == VK_UP) {
            x = -1;
        }
        if (x != 0) {
            UINT inv_scan = windowData->StaticData->Config->Key.Invert;
            UINT inv_vk = MapVirtualKey((inv_scan & 0xFF) | (inv_scan >> 8 ? 0xE000 : 0), MAPVK_VSC_TO_VK_EX);
            const bool invert = GetAsyncKeyState((int)inv_vk) & 0x8000;
            MoveSelection(windowData, invert ? -x : x);
            return 0;
        }

        // App close
        if (scan == windowData->StaticData->Config->Key.AppClose) {
            const SWinGroup* winGroup = &windowData->WinGroups.Data[windowData->Selection];
            CloseAppGroup(winGroup, windowData->MainWin);
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

    if (ProcessKeys(appData, uMsg, wParam, lParam) == 0)
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
    for (uint32_t i = 0; i < d->Count; i++) {
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

// Windows sends minimized windows to the bottom of the raw Z-order that
// EnumWindows() returns, which used to make them jump to the very end of
// the switcher regardless of how recently they were actually used.
// Re-order the groups by real activation recency (tracked independently
// via MruTrackerGetRank) instead, so a minimized window keeps the slot it
// had before being minimized. A group with no MRU history yet (e.g. right
// after AAS starts) keeps its relative Z-order position, since insertion
// sort is stable and unknown ranks all compare equal.
static void SortWinGroupsByRecency(SWinGroupArr* winGroups)
{
    for (uint32_t i = 1; i < winGroups->Size; i++) {
        const SWinGroup key = winGroups->Data[i];
        int keyRank = INT_MAX;
        for (uint32_t w = 0; w < key.WindowCount; w++) {
            const int rank = MruTrackerGetRank(key.Windows[w]);
            if (rank >= 0 && rank < keyRank)
                keyRank = rank;
        }

        uint32_t j = i;
        while (j > 0) {
            const SWinGroup* prev = &winGroups->Data[j - 1];
            int prevRank = INT_MAX;
            for (uint32_t w = 0; w < prev->WindowCount; w++) {
                const int rank = MruTrackerGetRank(prev->Windows[w]);
                if (rank >= 0 && rank < prevRank)
                    prevRank = rank;
            }
            if (prevRank <= keyRank)
                break;
            winGroups->Data[j] = winGroups->Data[j - 1];
            j--;
        }
        winGroups->Data[j] = key;
    }
}

static void Init(struct WindowData* windowData)
{
    ASSERT(windowData->StaticData);
    ASSERT(windowData->StaticData->Config);
    // Save persistant data and kill child window if any.
    struct StaticData* sd = windowData->StaticData;
    HWND w = windowData->MainWin;
    for (uint32_t i = 0; i < windowData->FocusWindowCount; i++) {
        if (!IsRunWindow(windowData->FocusWindows[i]))
            continue;
        DestroyWindow(windowData->FocusWindows[i]);
    }

    if (windowData->DC)
        DeleteDC(windowData->DC);
    if (windowData->Bitmap)
        DeleteObject(windowData->Bitmap);

    // Clear
    *windowData = (struct WindowData) { };

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
    StaticData.UWPIconMap.UseIdx++;
    EnumWindows(FillWinGroups, (LPARAM)windowData);
    SortWinGroupsByRecency(pWinGroups);

    const bool invert = GetAsyncKeyState((SHORT)windowData->StaticData->Config->Key.Invert) & 0x8000;
    windowData->Selection = Modulo(windowData->Selection + (invert ? -1 : 1), (int)windowData->WinGroups.Size);
    // Without common selection, a tile is only highlighted when hovered.
    windowData->MouseSelection = windowData->StaticData->Config->MouseKbCommonSel ? windowData->Selection : -1;
    windowData->LastSelection = windowData->Selection;
    GetCursorPos(&windowData->InitCursorPos);

    if (windowData->WinGroups.Size == 0)
        return;

    ComputeMetrics(windowData->WinGroups.Size,
        &windowData->Metrics,
        windowData->StaticData->Config);

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
    Draw(windowData, clientRect);
    SetLayeredWindowAttributes(windowData->MainWin, 0, 255, LWA_ALPHA);

    SetCapture(windowData->MainWin);

    for (uint32_t i = 0; i < windowData->WinGroups.Size; i++) {
        const int iconContainerSize = (int)windowData->Metrics.Container;
        const int pad = (int)windowData->Metrics.Pad;
        int x = pad + (int)(i * iconContainerSize);
        windowData->FocusWindows[i] = CreateWindowEx(0, FOCUS_CLASS_NAME, NULL,
            WS_CHILD /* | WS_VISIBLE */,
            x, pad, iconContainerSize, iconContainerSize,
            windowData->MainWin, NULL, windowData->StaticData->Instance, windowData);
        windowData->FocusWindowCount++;
    }
}

void CloseAppGroup(const SWinGroup* winGroup, HWND hwnd)
{
    static HANDLE ht = 0;
    static CloseThreadData ctd = { };
    if (ht)
        TerminateThread(ht, 0);
    ctd.Count = 0;

    ctd.MainWin = hwnd;
    for (uint32_t i = 0; i < winGroup->WindowCount; i++) {
        const HWND win = winGroup->Windows[i];
        ctd.Count++;
        ctd.Win[i] = win;
    }

    DWORD tid;
    ht = CreateThread(NULL, 0, CloseThread, (void*)&ctd, 0, &tid);

    for (uint32_t i = 0; i < winGroup->WindowCount; i++) {
        const HWND win = winGroup->Windows[i];
        PostMessage(win, WM_CLOSE, 0, 0);
    }
}

static LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static struct WindowData windowData = { };

    if (ProcessKeys(&windowData, uMsg, wParam, lParam) == 0)
        return 0;

    static bool firstMouseMove = true;

    switch (uMsg) {
    case WM_MOUSEMOVE: {
        ASSERT(windowData.StaticData);
        ASSERT(windowData.StaticData->Config);
        // Otherwise cursor is busy on hover. I don't understand why.
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        if (!windowData.StaticData->Config->Mouse)
            return 0;
        if (windowData.StaticData->Config->MouseKbCommonSel && firstMouseMove) {
            firstMouseMove = false;
            return 0;
        }
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        if (windowData.StaticData->Config->MouseKbCommonSel) {
            windowData.MouseSelection = (int)PosToIdx(&windowData.Metrics, x, y, (int)windowData.WinGroups.Size);
            windowData.Selection = windowData.MouseSelection;
        } else {
            // Windows sends a mouse move when the switcher appears under the
            // cursor: only hover once the mouse has actually moved.
            if (!windowData.MouseMoved) {
                POINT cursorPos;
                GetCursorPos(&cursorPos);
                if (cursorPos.x == windowData.InitCursorPos.x && cursorPos.y == windowData.InitCursorPos.y)
                    return 0;
                windowData.MouseMoved = true;
            }
            windowData.MouseSelection = TileAtPos(&windowData.Metrics, x, y, windowData.WinGroups.Size);
        }
        windowData.CloseHover = false;
        if (windowData.MouseSelection >= 0) {
            windowData.LastSelection = windowData.MouseSelection;
            float r[4];
            CloseButtonRect(r, &windowData.Metrics, (uint32_t)windowData.MouseSelection);
            windowData.CloseHover = x < (int)r[2] && x > (int)r[0] && y > (int)r[1] && y < (int)r[3];
        }
        InvalidateRect(windowData.MainWin, 0, FALSE);
        UpdateWindow(windowData.MainWin);
        return 0;
    }
    case WM_CREATE: {
        firstMouseMove = true;
        windowData = (struct WindowData) { };
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
        SetFocus(windowData.FocusWindows[windowData.Selection]);
        return 0;
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
            SelfClose(&windowData.MainWin);
            ClearWinGroupArr(&windowData.WinGroups);
            return 0;
        }
        if (!windowData.StaticData->Config->Mouse)
            return 0;
        // Click inside the switcher but not on a tile.
        if (windowData.MouseSelection < 0)
            return 0;

        if (windowData.CloseHover) {
            const SWinGroup* winGroup = &windowData.WinGroups.Data[windowData.MouseSelection];
            CloseAppGroup(winGroup, hwnd);

            return 0;
        }
#ifdef ASYNC_APPLY
        ApplyWithTimeout(ApplyMouse, &windowData, windowData.StaticData->Instance);
#else
        ApplySwitchApp(&appData->WinGroups.Data[appData->MouseSelection]);
#endif
        windowData.Selection = 0;
        windowData.MouseSelection = -1;
        windowData.LastSelection = 0;
        windowData.CloseHover = false;
        SelfClose(&windowData.MainWin);
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
        windowData.MouseSelection = -1;
        windowData.LastSelection = 0;
        ClearWinGroupArr(&windowData.WinGroups);
        DeleteDC(windowData.DC);
        DeleteObject(windowData.Bitmap);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps = { };
        if (BeginPaint(hwnd, &ps) == NULL) {
            ASSERT(false);
            return 0;
        }
        RECT clientRect;
        ASSERT(GetClientRect(hwnd, &clientRect));
        Draw(&windowData, clientRect);
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
