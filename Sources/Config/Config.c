#include "Config.h"
#include <stdbool.h>
#include <stdio.h>
#include <Winuser.h>
#include <stdlib.h>
#include <debugapi.h>
#include <cJSON/cJSON.h>
#include <errno.h>
#include <string.h>
#include "Utils/Error.h"
#include "Utils/File.h"

#define AAS_NONE_VK 0xFFFFFFFE
#define VK_Q (0x51)
const EnumString keyES[17] = {
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
    { "q", VK_Q },
    { "f4", VK_F4 },
    { "none", AAS_NONE_VK },
    { "end", 0xFFFFFFFF },
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

static bool GetBoolOld(const StrPair* keyValues, const char* token, bool* boolToSet)
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

static bool GetFloatOld(const StrPair* keyValues, const char* token, float* floatToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    *floatToSet = strtof(keyValues[entry].Value, NULL);
    return true;
}

static bool GetIntOld(const StrPair* keyValues, const char* token, int* intToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    *intToSet = (int)strtol(keyValues[entry].Value, NULL, 10);
    return true;
}

static bool GetEnumOld(const StrPair* keyValues, const char* token,
    unsigned int* outValue, const EnumString* enumStrings)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF) {
        return false;
    }
    for (unsigned int i = 0; enumStrings[i].ValUInt != 0xFFFFFFFF; i++) {
        if (!strcmp(keyValues[entry].Value, enumStrings[i].ValStr)) {
            *outValue = enumStrings[i].ValUInt;
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
    config->Key.AppClose = VK_Q;
    config->Mouse = true;
    config->MouseKbCommonSel = false;
    config->CheckForUpdates = true;
    config->ThemeMode = ThemeModeAuto;
    config->AppSwitcherMode = AppSwitcherModeApp;
    config->Scale = 2.5f;
    config->DisplayName = DisplayNameSel;
    config->MultipleMonitorMode = MultipleMonitorModeMouse;
    config->AppFilterMode = AppFilterModeAll;
    config->RestoreMinimizedWindows = true;
    config->DesktopFilter = DesktopFilterCurrent;
    config->IconsPerRow = 0;
}

// Init config from old (non json) file.
// If not found, lead config untouched.
static void ReadConfigOld(Config* config)
{
    DefaultConfig(config);
    char configFile[MAX_PATH] = { };
    // Old cfg file
    {
        configFile[0] = '\0';
        char currentExe[MAX_PATH] = { };
        GetModuleFileName(NULL, currentExe, MAX_PATH);
        ParentDir(currentExe, configFile);
        strcat_s(configFile, sizeof(char) * MAX_PATH, "/AltAppSwitcherConfig.txt");
    }
    FILE* file = fopen(configFile, "rb");
    if (file == NULL) {
        return;
    }

#define GET_ENUM(ENTRY, DST, ENUM_STRING) \
    GetEnumOld(keyValues, ENTRY, &(DST), ENUM_STRING)

#define GET_BOOL(ENTRY, DST) \
    GetBoolOld(keyValues, ENTRY, &(DST))

#define GET_FLOAT(ENTRY, DST) \
    GetFloatOld(keyValues, ENTRY, &(DST))

    static StrPair keyValues[32] = { };

    static char lineBuf[1024] = { };
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
    GET_ENUM("close app key", config->Key.AppClose, keyES);

    GET_ENUM("theme", config->ThemeMode, themeES);
    GET_ENUM("app switcher mode", config->AppSwitcherMode, appSwitcherModeES);
    GET_ENUM("display name", config->DisplayName, displayNameES);
    GET_ENUM("multiple monitor mode", config->MultipleMonitorMode, multipleMonitorModeES);
    GET_ENUM("app filter mode", config->AppFilterMode, appFilterModeES);
    GET_ENUM("desktop filter", config->DesktopFilter, desktopFilterES);
    GET_BOOL("restore minimized windows", config->RestoreMinimizedWindows);

    GET_BOOL("allow mouse", config->Mouse);
    GET_BOOL("mouse keyboard common selection", config->MouseKbCommonSel);
    GET_BOOL("check for updates", config->CheckForUpdates);

    GET_FLOAT("scale", config->Scale);

    GetIntOld(keyValues, "icons per row", &config->IconsPerRow);
#undef GET_ENUM
#undef GET_BOOL
#undef GET_FLOAT
}

static void JSONReadEnum(const cJSON* parentObj, const char* key,
    unsigned int* outValue, const EnumString* enumStrings)
{
    const cJSON* obj = cJSON_GetObjectItem(parentObj, key);
    if (!obj || !cJSON_IsString(obj))
        return;
    const EnumString* es = enumStrings;
    const char* valueStr = cJSON_GetStringValue(obj);
    while (es->ValUInt != 0xFFFFFFFF) {
        if (!strcmp(es->ValStr, valueStr)) {
            *outValue = es->ValUInt;
            return;
        }
        es++;
    }
}

static void JSONReadBool(const cJSON* parentObj, const char* key,
    bool* outValue)
{
    const cJSON* obj = cJSON_GetObjectItem(parentObj, key);
    if (!obj || !cJSON_IsBool(obj))
        return;
    *outValue = cJSON_IsTrue(obj);
}

static void JSONReadFloat(const cJSON* parentObj, const char* key,
    float* outValue)
{
    const cJSON* obj = cJSON_GetObjectItem(parentObj, key);
    if (!obj || !cJSON_IsNumber(obj))
        return;
    *outValue = (float)cJSON_GetNumberValue(obj);
}

static void JSONReadInt(const cJSON* parentObj, const char* key,
    int* outValue)
{
    const cJSON* obj = cJSON_GetObjectItem(parentObj, key);
    if (!obj || !cJSON_IsNumber(obj))
        return;
    *outValue = (int)cJSON_GetNumberValue(obj);
}

static void JSONWriteEnum(cJSON* parentObj, const char* key,
    unsigned int inValue, const EnumString* enumStrings)
{
    const EnumString* es = enumStrings;
    while (es->ValUInt != 0xFFFFFFFF) {
        if (es->ValUInt == inValue) {
            cJSON_AddStringToObject(parentObj, key, es->ValStr);
            return;
        }
        es++;
    }
}

static void JSONWriteFloat(cJSON* parentObj, const char* key,
    float inValue)
{
    cJSON_AddNumberToObject(parentObj, key, inValue);
}

static void JSONWriteInt(cJSON* parentObj, const char* key,
    int inValue)
{
    cJSON_AddNumberToObject(parentObj, key, inValue);
}

static void JSONWriteBool(cJSON* parentObj, const char* key,
    bool inValue)
{
    cJSON_AddBoolToObject(parentObj, key, inValue);
}

void LoadConfig(Config* config)
{
    DefaultConfig(config);
    char configFile[MAX_PATH] = { };
    ConfigPath(configFile);
    FILE* file = fopen(configFile, "rb");
    if (file == NULL) {
        ReadConfigOld(config);
        WriteConfig(config);
        return;
    }
    cJSON* j = NULL;
    {
        (void)fseek(file, 0, SEEK_END);
        long size = ftell(file);
        char* data = malloc(sizeof(char) * size);
        (void)fseek(file, 0, SEEK_SET);
        long readSize = (long)fread(data, sizeof(char), size, file);
        ASSERT(size == readSize);
        j = cJSON_ParseWithLength(data, readSize);
        ASSERT(j != NULL);
        free(data);
    }
    {
        int r = fclose(file);
        ASSERT(r == 0);
    }
    // Keys: todo
#if 0
    GET_ENUM("app hold key", config->Key.AppHold, keyES);
    GET_ENUM("next app key", config->Key.AppSwitch, keyES);
    GET_ENUM("window hold key", config->Key.WinHold, keyES);
    GET_ENUM("next window key", config->Key.WinSwitch, keyES);
    GET_ENUM("invert order key", config->Key.Invert, keyES);
    GET_ENUM("previous app key", config->Key.PrevApp, keyES);
    GET_ENUM("close app key", config->Key.AppClose, keyES);
#endif

    JSONReadEnum(j, "theme", &config->ThemeMode, themeES);
    JSONReadEnum(j, "app_switcher_mode", &config->AppSwitcherMode, appSwitcherModeES);
    JSONReadEnum(j, "display_name", &config->DisplayName, displayNameES);
    JSONReadEnum(j, "multiple_monitor_mode", &config->MultipleMonitorMode, multipleMonitorModeES);
    JSONReadEnum(j, "app_filter_mode", &config->AppFilterMode, appFilterModeES);
    JSONReadEnum(j, "desktop_filter", &config->DesktopFilter, desktopFilterES);

    JSONReadBool(j, "restore minimized windows", &config->RestoreMinimizedWindows);

    JSONReadBool(j, "allow_mouse", &config->Mouse);
    JSONReadBool(j, "mouse_keyboard_common_selection", &config->MouseKbCommonSel);
    JSONReadBool(j, "check_for_updates", &config->CheckForUpdates);

    JSONReadFloat(j, "scale", &config->Scale);

    JSONReadInt(j, "icons_per_row", &config->IconsPerRow);
}

void WriteConfig(const Config* config)
{
    cJSON* j = cJSON_CreateObject();
#if 0
    WRITE_ENUM("app hold key", config->Key.AppHold, keyES);
    WRITE_ENUM("next app key", config->Key.AppSwitch, keyES);
    WRITE_ENUM("window hold key", config->Key.WinHold, keyES);
    WRITE_ENUM("next window key", config->Key.WinSwitch, keyES);
    WRITE_ENUM("invert order key", config->Key.Invert, keyES);
    WRITE_ENUM("previous app key", config->Key.PrevApp, keyES);
    WRITE_ENUM("close app key", config->Key.AppClose, keyES);
#endif
    JSONWriteEnum(j, "theme", config->ThemeMode, themeES);
    JSONWriteEnum(j, "app_switcher_mode", config->AppSwitcherMode, appSwitcherModeES);
    JSONWriteEnum(j, "display_name", config->DisplayName, displayNameES);
    JSONWriteEnum(j, "multiple_monitor_ mode", config->MultipleMonitorMode, multipleMonitorModeES);
    JSONWriteEnum(j, "app_filter_mode", config->AppFilterMode, appFilterModeES);
    JSONWriteEnum(j, "desktop_filter", config->DesktopFilter, desktopFilterES);

    JSONWriteBool(j, "restore_minimized_windows", config->RestoreMinimizedWindows);

    JSONWriteBool(j, "allow_mouse", config->Mouse);
    JSONWriteBool(j, "mouse_keyboard_common_selection", config->MouseKbCommonSel);
    JSONWriteBool(j, "check_for_updates", config->CheckForUpdates);

    JSONWriteFloat(j, "scale", config->Scale);

    JSONWriteInt(j, "icons_per_row", config->IconsPerRow);

    char* jsonstr = cJSON_Print(j);

    {
        char configFile[MAX_PATH] = { };
        ConfigPath(configFile);
        FILE* file = fopen(configFile, "w");
        ASSERT(file);
        {
            int r = fputs(jsonstr, file);
            ASSERT(r == 0);
        }
        {
            int r = fclose(file);
            ASSERT(r == 0);
        }
    }

    cJSON_free(jsonstr);
    cJSON_Delete(j);
}
