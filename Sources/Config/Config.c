#include "Config.h"
#include <stdbool.h>
#include <stdio.h>
#include <Winuser.h>
#include <stdlib.h>
#include <debugapi.h>
#include "Utils/Error.h"
#include "Utils/File.h"

#define AAS_NONE_VK 0xFFFFFFFE
const EnumString keyES[15] = {
    { "left alt", VK_LMENU },
    { "right alt", VK_RMENU },
    { "alt", VK_MENU },
    { "tilde", VK_OEM_3 }, // Scan code 41
    { "left windows", VK_LWIN },
    { "right windows", VK_RWIN },
    { "right super", VK_RWIN },
    { "left super", VK_LWIN },
    { "left control", VK_LCONTROL },
    { "right control", VK_RCONTROL },
    { "left shift", VK_LSHIFT },
    { "right shift", VK_RSHIFT },
    { "tab", VK_TAB },
    { "none", AAS_NONE_VK },
    { "end", 0xFFFFFFFF }
};

const EnumString themeES[4] = {
    { "auto", ThemeModeAuto },
    { "light", ThemeModeLight },
    { "dark", ThemeModeDark },
    { "end", 0xFFFFFFFF }
};

const EnumString appSwitcherModeES[3] = {
    { "app", AppSwitcherModeApp },
    { "window", AppSwitcherModeWindow },
    { "end", 0xFFFFFFFF }
};

const EnumString displayNameES[4] = {
    { "selected", DisplayNameSel },
    { "all", DisplayNameAll },
    { "none", DisplayNameNone },
    { "end", 0xFFFFFFFF }
};

const EnumString multipleMonitorModeES[3] = {
    { "mouse", MultipleMonitorModeMouse },
    { "main", MultipleMonitorModeMain },
    { "end", 0xFFFFFFFF }
};

const EnumString appFilterModeES[3] = {
    { "all", AppFilterModeAll },
    { "mouse monitor", AppFilterModeMouseMonitor },
    { "end", 0xFFFFFFFF }
};

const EnumString desktopFilterES[3] = {
    { "current", DesktopFilterCurrent },
    { "all", DesktopFilterAll },
    { "end", 0xFFFFFFFF }
};

typedef struct StrPair {
    char Key[64];
    char Value[64];
} StrPair;

static unsigned int Find(const StrPair* keyValues, const char* key)
{
    for (unsigned int i = 0; i < 32; i++) {
        if (!strcmp(keyValues[i].Key, key))
            return i;
    }
    return 0xFFFFFFFF;
}

static bool TryGetBool(const StrPair* keyValues, const char* token, bool* boolToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    if (strstr(keyValues[entry].Value, "true") != NULL) {
        *boolToSet = true;
        return true;
    }
    if (strstr(keyValues[entry].Value, "false") != NULL) {
        *boolToSet = false;
        return true;
    }
    return false;
}

static bool TryGetFloat(const StrPair* keyValues, const char* token, float* floatToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    *floatToSet = strtof(keyValues[entry].Value, NULL);
    return true;
}

static bool TryGetEnum(const StrPair* keyValues, const char* token,
    unsigned int* outValue, const EnumString* enumStrings)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++) {
        if (!strcmp(keyValues[entry].Value, enumStrings[i].Name)) {
            *outValue = enumStrings[i].Value;
            return true;
        }
    }
    ASSERT(false)
    return false;
}

void DefaultConfig(Config* config)
{
    config->Key.AppHold = VK_LMENU;
    config->Key.AppSwitch = VK_TAB;
    config->Key.WinHold = VK_LMENU;
    config->Key.WinSwitch = VK_OEM_3;
    config->Key.Invert = VK_LSHIFT;
    config->Key.PrevApp = VK_OEM_3;
    config->Mouse = true;
    config->CheckForUpdates = true;
    config->ThemeMode = ThemeModeAuto;
    config->AppSwitcherMode = AppSwitcherModeApp;
    config->Scale = 2.5f;
    config->DisplayName = DisplayNameSel;
    config->MultipleMonitorMode = MultipleMonitorModeMouse;
    config->AppFilterMode = AppFilterModeAll;
    config->RestoreMinimizedWindows = true;
    config->DesktopFilter = DesktopFilterCurrent;
    config->DebugDisableIconFocus = false;
}

void LoadConfig(Config* config)
{
    DefaultConfig(config);
    char configFile[MAX_PATH] = {};
    ConfigPath(configFile);
    FILE* file = fopen(configFile, "rb");
    if (file == NULL) {
        WriteConfig(config);
        return;
    }

#define GET_ENUM(ENTRY, DST, ENUM_STRING) \
    TryGetEnum(keyValues, ENTRY, &(DST), ENUM_STRING)

#define GET_BOOL(ENTRY, DST) \
    TryGetBool(keyValues, ENTRY, &(DST))

#define GET_FLOAT(ENTRY, DST) \
    TryGetFloat(keyValues, ENTRY, &(DST))

    static StrPair keyValues[32] = {};

    static char lineBuf[1024] = {};
    unsigned int i = 0;
    while (fgets(lineBuf, 1024, file)) {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        const char* sep = strstr(lineBuf, ": ");
        if (sep == NULL)
            continue;
        const char* end = strstr(lineBuf, "\r\n");
        if (end == NULL)
            continue;
        strncpy_s(keyValues[i].Key, sizeof(keyValues[i].Key), lineBuf, sizeof(char) * (sep - lineBuf));
        strncpy_s(keyValues[i].Value, sizeof(keyValues[i].Value), sep + 2, sizeof(char) * (end - (sep + 2)));
        i++;
    }
    int a = fclose(file);
    ASSERT(a == 0);

    GET_ENUM("app hold key", config->Key.AppHold, keyES);
    GET_ENUM("next app key", config->Key.AppSwitch, keyES);
    GET_ENUM("window hold key", config->Key.WinHold, keyES);
    GET_ENUM("next window key", config->Key.WinSwitch, keyES);
    GET_ENUM("invert order key", config->Key.Invert, keyES);
    GET_ENUM("previous app key", config->Key.PrevApp, keyES);

    GET_ENUM("theme", config->ThemeMode, themeES);
    GET_ENUM("app switcher mode", config->AppSwitcherMode, appSwitcherModeES);
    GET_ENUM("display name", config->DisplayName, displayNameES);
    GET_ENUM("multiple monitor mode", config->MultipleMonitorMode, multipleMonitorModeES);
    GET_ENUM("app filter mode", config->AppFilterMode, appFilterModeES);
    GET_ENUM("desktop filter", config->DesktopFilter, desktopFilterES);
    GET_BOOL("restore minimized windows", config->RestoreMinimizedWindows);
    GET_BOOL("debug disable icon focus", config->DebugDisableIconFocus);

    GET_BOOL("allow mouse", config->Mouse);
    GET_BOOL("check for updates", config->CheckForUpdates);

    GET_FLOAT("scale", config->Scale);

#undef GET_ENUM
#undef GET_BOOL
#undef GET_FLOAT
}

static void WriteEnum(FILE* file, const char* entry,
    unsigned int value, const EnumString* enumStrings)
{
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++) {
        if (enumStrings[i].Value == value) {
            size_t a = fprintf_s(file, "%s: %s\n", entry, enumStrings[i].Name);
            (void)a;
            return;
        }
    }
    ASSERT(false)
}

static void WriteBool(FILE* file, const char* entry, bool value)
{
    size_t a = fprintf_s(file, "%s: %s\n", entry, value ? "true" : "false");
    ASSERT(a > 0);
}

static void WriteFloat(FILE* file, const char* entry, float value)
{
    size_t a = fprintf_s(file, "%s: %f\n", entry, value);
    ASSERT(a > 0);
}

void WriteConfig(const Config* config)
{
    char configFile[MAX_PATH] = {};
    ConfigPath(configFile);
    FILE* file = fopen(configFile, "w");
    ASSERT(file);
    if (!file)
        return;

#define WRITE_ENUM(ENTRY, VALUE, ENUM_STRING) \
    WriteEnum(file, ENTRY, VALUE, ENUM_STRING)

#define WRITE_BOOL(ENTRY, VALUE) \
    WriteBool(file, ENTRY, VALUE)

#define WRITE_FLOAT(ENTRY, VALUE) \
    WriteFloat(file, ENTRY, VALUE)

    WRITE_ENUM("app hold key", config->Key.AppHold, keyES);
    WRITE_ENUM("next app key", config->Key.AppSwitch, keyES);
    WRITE_ENUM("window hold key", config->Key.WinHold, keyES);
    WRITE_ENUM("next window key", config->Key.WinSwitch, keyES);
    WRITE_ENUM("invert order key", config->Key.Invert, keyES);
    WRITE_ENUM("previous app key", config->Key.PrevApp, keyES);
    WRITE_ENUM("theme", config->ThemeMode, themeES);
    WRITE_ENUM("app switcher mode", config->AppSwitcherMode, appSwitcherModeES);
    WRITE_ENUM("display name", config->DisplayName, displayNameES);
    WRITE_ENUM("multiple monitor mode", config->MultipleMonitorMode, multipleMonitorModeES);
    WRITE_ENUM("app filter mode", config->AppFilterMode, appFilterModeES);
    WRITE_ENUM("desktop filter", config->DesktopFilter, desktopFilterES);
    WRITE_BOOL("restore minimized windows", config->RestoreMinimizedWindows);
    WRITE_BOOL("debug disable icon focus", config->DebugDisableIconFocus);

    WRITE_BOOL("allow mouse", config->Mouse);
    WRITE_BOOL("check for updates", config->CheckForUpdates);

    WRITE_FLOAT("scale", config->Scale);

    const int r = fclose(file);
    ASSERT(r == 0);

#undef WRITE_ENUM
#undef WRITE_BOOL
#undef WRITE_FLOAT
}
