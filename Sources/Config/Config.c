#include "Config.h"
#include "_Generated/ConfigStr.h"
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

const EnumString appSwitcherModeES[3] =
{
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

typedef struct StrPair
{
    char Key[64];
    char Value[64];
} StrPair;

static unsigned int Find(const StrPair* keyValues, const char* key)
{
    for (unsigned int i = 0; i < 32; i++)
    {
        if (!strcmp(keyValues[i].Key, key))
            return i;
    }
    return 0xFFFFFFFF;
}

static bool TryGetBool(const StrPair* keyValues, const char* token, bool* boolToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF)
    {
        return false;
    }
    if (strstr(keyValues[entry].Value, "true") != NULL)
    {
        *boolToSet = true;
        return true;
    }
    else if (strstr(keyValues[entry].Value, "false") != NULL)
    {
        *boolToSet = false;
        return true;
    }
    return false;
}

static bool TryGetFloat(const StrPair* keyValues, const char* token, float* floatToSet)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF)
    {
        DebugBreak();
        return false;
    }
    *floatToSet = strtof(keyValues[entry].Value, NULL);
    return true;
}

static bool TryGetEnum(const StrPair* keyValues, const char* token,
    unsigned int* outValue, const EnumString* enumStrings, unsigned int defaultValue)
{
    unsigned int entry = Find(keyValues, token);
    if (entry == 0xFFFFFFFF)
    {
        *outValue = defaultValue;
        return false;
    }
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        if (!strcmp(keyValues[entry].Value, enumStrings[i].Name))
        {
            *outValue = enumStrings[i].Value;
            return true;
        }
    }
    ASSERT(false)
    return false;
}

void LoadConfig(Config* config)
{
    char configFile[MAX_PATH] = {};
    ConfigPath(configFile);
    FILE* file = fopen(configFile ,"rb");
    if (file == NULL)
    {
        file = fopen(configFile ,"a");
        fprintf(file, ConfigStr);
        fclose(file);
        fopen(configFile ,"rb");
    }

#define GET_ENUM(ENTRY, DST, ENUM_STRING, DEFAULT)\
TryGetEnum(keyValues, ENTRY, &DST, ENUM_STRING, DEFAULT)

#define GET_BOOL(ENTRY, DST)\
TryGetBool(keyValues, ENTRY, &DST)

#define GET_FLOAT(ENTRY, DST)\
TryGetFloat(keyValues, ENTRY, &DST)

    static StrPair keyValues[32];
    memset(keyValues, 0x0, sizeof(keyValues));

    static char lineBuf[1024];
    unsigned int i = 0;
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        const char* sep = strstr(lineBuf, ": ");
        if (sep == NULL)
            continue;
        const char* end = strstr(lineBuf, "\r\n");
        if (end == NULL)
            continue;
        strncpy(keyValues[i].Key, lineBuf, sizeof(char) * (sep - lineBuf));
        strncpy(keyValues[i].Value, sep + 2, sizeof(char) * (end - (sep + 2)));
        i++;
    }
    fclose(file);

    GET_ENUM("app hold key", config->_Key._AppHold, keyES, AAS_NONE_VK);
    GET_ENUM("next app key", config->_Key._AppSwitch, keyES, AAS_NONE_VK);
    GET_ENUM("window hold key", config->_Key._WinHold, keyES, AAS_NONE_VK);
    GET_ENUM("next window key", config->_Key._WinSwitch, keyES, AAS_NONE_VK);
    GET_ENUM("invert order key", config->_Key._Invert, keyES, AAS_NONE_VK);
    GET_ENUM("previous app key", config->_Key._PrevApp, keyES, AAS_NONE_VK);

    GET_ENUM("theme", config->_ThemeMode, themeES, ThemeModeAuto);
    GET_ENUM("app switcher mode", config->_AppSwitcherMode, appSwitcherModeES, AppSwitcherModeApp);
    GET_ENUM("display name", config->_DisplayName, displayNameES, DisplayNameSel);
    GET_ENUM("multiple monitor mode", config->_MultipleMonitorMode, multipleMonitorModeES, MultipleMonitorModeMouse);

    GET_BOOL("allow mouse", config->_Mouse);
    GET_BOOL("check for updates", config->_CheckForUpdates);

    GET_FLOAT("scale", config->_Scale);

#undef GET_ENUM
#undef GET_BOOL
#undef GET_FLOAT
}

static void WriteEnum(FILE* file, const char* entry,
    unsigned int value, const EnumString* enumStrings)
{
    for (unsigned int i = 0; enumStrings[i].Value != 0xFFFFFFFF; i++)
    {
        if (enumStrings[i].Value == value)
        {
            fprintf(file, "%s: %s\n", entry, enumStrings[i].Name);
            return;
        }
    }
    ASSERT(false)
}

static void WriteBool(FILE* file, const char* entry, bool value)
{
    fprintf(file, "%s: %s\n", entry, value ? "true" : "false");
}

static void WriteFloat(FILE* file, const char* entry, float value)
{
    fprintf(file, "%s: %f\n", entry, value);
}

void WriteConfig(const Config* config)
{
    char configFile[MAX_PATH] = {};
    ConfigPath(configFile);
    FILE* file = fopen(configFile ,"w");

#define WRITE_ENUM(ENTRY, VALUE, ENUM_STRING)\
WriteEnum(file, ENTRY, VALUE, ENUM_STRING)

#define WRITE_BOOL(ENTRY, VALUE)\
WriteBool(file, ENTRY, VALUE)

#define WRITE_FLOAT(ENTRY, VALUE)\
WriteFloat(file, ENTRY, VALUE)

    WRITE_ENUM("app hold key", config->_Key._AppHold, keyES);
    WRITE_ENUM("next app key", config->_Key._AppSwitch, keyES);
    WRITE_ENUM("window hold key", config->_Key._WinHold, keyES);
    WRITE_ENUM("next window key", config->_Key._WinSwitch, keyES);
    WRITE_ENUM("invert order key", config->_Key._Invert, keyES);
    WRITE_ENUM("previous app key", config->_Key._PrevApp, keyES);
    WRITE_ENUM("theme", config->_ThemeMode, themeES);
    WRITE_ENUM("app switcher mode", config->_AppSwitcherMode, appSwitcherModeES);
    WRITE_ENUM("display name", config->_DisplayName, displayNameES);
    WRITE_ENUM("multiple monitor mode", config->_MultipleMonitorMode, multipleMonitorModeES);

    WRITE_BOOL("allow mouse", config->_Mouse);
    WRITE_BOOL("check for updates", config->_CheckForUpdates);

    WRITE_FLOAT("scale", config->_Scale);

    fclose(file);
#undef WRITE_ENUM
#undef WRITE_BOOL
#undef WRITE_FLOAT
}
