#include "Config.h"
#include "_Generated/ConfigStr.h"
#include <stdbool.h>
#include <stdio.h>
#include <Winuser.h>
#include <stdlib.h>

const EnumString keyEnum[14] = {
    { "left alt", VK_LMENU },
    { "right alt", VK_RMENU },
    { "alt", VK_MENU },
    { "tilde", VK_OEM_3 },
    { "left windows", VK_LWIN },
    { "right windows", VK_RWIN },
    { "right super", VK_RWIN },
    { "left super", VK_LWIN },
    { "left control", VK_LCONTROL },
    { "right control", VK_RCONTROL },
    { "left shift", VK_LSHIFT },
    { "right shift", VK_RSHIFT },
    { "tab", VK_TAB },
    { "none", 0xFFFFFFFF },
};

const EnumString themeEnum[3] = {
    { "auto", ThemeModeAuto },
    { "light", ThemeModeLight },
    { "dark", ThemeModeDark }
};

const EnumString appSwitcherModeEnum[2] =
{
    { "app", AppSwitcherModeApp },
    { "window", AppSwitcherModeApp }
};

static bool TryGetBool(const char* lineBuf, const char* token, bool* boolToSet)
{
    char fullToken[256];
    strcpy(fullToken, token);
    strcat(fullToken, ": ");
    const char* pValue = strstr(lineBuf, fullToken);
    if (pValue != NULL)
    {
        if (strstr(pValue + strlen(fullToken) - 1, "true") != NULL)
        {
            *boolToSet = true;
            return true;
        }
        else if (strstr(pValue + strlen(fullToken) - 1, "false") != NULL)
        {
            *boolToSet = false;
            return true;
        }
    }
    return false;
}

static bool TryGetFloat(const char* lineBuf, const char* token, float* floatToSet)
{
    char fullToken[256];
    strcpy(fullToken, token);
    strcat(fullToken, ": ");
    const char* pValue = strstr(lineBuf, fullToken);
    if (pValue != NULL)
    {
        *floatToSet = strtof(pValue + strlen(fullToken)  - 1, NULL);
        return true;
    }
    return false;
}

static bool TryGetEnum(const char* lineBuf, const char* token,
    unsigned int* outValue, const EnumString* enumStrings, unsigned int enumCount)
{
    char fullToken[256];
    strcpy(fullToken, token);
    strcat(fullToken, ": ");
    const char* pValue = strstr(lineBuf, fullToken);
    if (pValue == NULL)
        return false;
    for (unsigned int i = 0; i < enumCount; i++)
    {
        if (strstr(pValue + strlen(fullToken) - 1, enumStrings[i].Name) != NULL)
        {
            *outValue = enumStrings[i].Value;
            return true;
        }
    }
    return false;
}


void LoadConfig(Config* config)
{
    const char* configFile = "AltAppSwitcherConfig.txt";
    FILE* file = fopen(configFile ,"rb");
    if (file == NULL)
    {
        file = fopen(configFile ,"a");
        fprintf(file, ConfigStr);
        fclose(file);
        fopen(configFile ,"rb");
    }

#define GET_ENUM(ENTRY, DST, ENUM_STRING)\
if (TryGetEnum(lineBuf, ENTRY, &DST, ENUM_STRING, sizeof(ENUM_STRING) / sizeof(ENUM_STRING[0])))\
    continue;

#define GET_BOOL(ENTRY, DST)\
if (TryGetBool(lineBuf,  ENTRY, &DST))\
    continue;

#define GET_FLOAT(ENTRY, DST)\
if (TryGetFloat(lineBuf, ENTRY, &DST))\
    continue;

    static char lineBuf[1024];
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        GET_ENUM("app hold key", config->_Key._AppHold, keyEnum)
        GET_ENUM("next app key", config->_Key._AppSwitch, keyEnum)
        GET_ENUM("window hold key", config->_Key._WinHold, keyEnum)
        GET_ENUM("next window key", config->_Key._WinSwitch, keyEnum)
        GET_ENUM("invert order key", config->_Key._Invert, keyEnum)
        GET_ENUM("previous app key", config->_Key._PrevApp, keyEnum)
        GET_ENUM("theme", config->_ThemeMode, themeEnum)
        GET_ENUM("app switcher mode", config->_AppSwitcherMode, appSwitcherModeEnum)

        GET_BOOL("allow mouse", config->_Mouse)
        GET_BOOL("check for updates", config->_CheckForUpdates)

        GET_FLOAT("scale", config->_Scale)
    }
    fclose(file);

#undef GET_ENUM
#undef GET_BOOL
#undef GET_FLOAT
}

static void WriteEnum(FILE* file, const char* entry,
    unsigned int value, const EnumString* enumStrings, unsigned int enumCount)
{
    unsigned int i = 0;
    for (; i < enumCount; i++)
    {
        if (enumStrings[i].Value == value)
        {
            fprintf(file, "%s: %s\n", entry, enumStrings[i].Name);
            return;
        }
    }
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
    const char* configFile = "AltAppSwitcherConfig0.txt";
    FILE* file = fopen(configFile ,"w");

#define WRITE_ENUM(ENTRY, VALUE, ENUM_STRING)\
WriteEnum(file, ENTRY, VALUE, ENUM_STRING, sizeof(ENUM_STRING) / sizeof(ENUM_STRING[0]))

#define WRITE_BOOL(ENTRY, VALUE)\
WriteBool(file, ENTRY, VALUE)

#define WRITE_FLOAT(ENTRY, VALUE)\
WriteFloat(file, ENTRY, VALUE)

    WRITE_ENUM("app hold key", config->_Key._AppHold, keyEnum);
    WRITE_ENUM("next app key", config->_Key._AppSwitch, keyEnum);
    WRITE_ENUM("window hold key", config->_Key._WinHold, keyEnum);
    WRITE_ENUM("next window key", config->_Key._WinSwitch, keyEnum);
    WRITE_ENUM("previous app key", config->_Key._PrevApp, keyEnum);
    WRITE_ENUM("invert order key", config->_Key._Invert, keyEnum);
    WRITE_ENUM("theme", config->_ThemeMode, themeEnum);
    WRITE_ENUM("app switcher mode", config->_AppSwitcherMode, appSwitcherModeEnum);

    WRITE_BOOL("allow mouse", config->_Mouse);
    WRITE_BOOL("check for updates", config->_CheckForUpdates);

    WRITE_FLOAT("scale", config->_Scale);

    fclose(file);
#undef WRITE_ENUM
#undef WRITE_BOOL
#undef WRITE_FLOAT
}
