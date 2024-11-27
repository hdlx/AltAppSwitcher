#include "Config.h"
#include "_Generated/ConfigStr.h"
#include "WinKeyCodes.h"
#include <stdbool.h>
#include <stdio.h>
#include <Winuser.h>
#include <stdlib.h>

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

typedef struct EnumString
{
    char* Name;
    unsigned int KeyCode;
} EnumString;

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
            *outValue = i;
            return true;
        }
    }
    return false;
}

void LoadConfig(Config* config)
{
    config->_Key._AppHold = VK_LMENU;
    config->_Key._AppSwitch = VK_TAB;
    config->_Key._WinHold = VK_LMENU;
    config->_Key._WinSwitch = VK_OEM_3;
    config->_Key._Invert = VK_LSHIFT;
    config->_Key._PrevApp = 0xFFFFFFFF;
    config->_Mouse = true;
    config->_CheckForUpdates = true;
    config->_ThemeMode = ThemeModeAuto;
    config->_AppSwitcherMode = AppSwitcherModeApp;
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

    static const EnumString keyEnum[] = {
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

    static const EnumString themeEnum[] = {
        { "auto", ThemeModeAuto },
        { "light", ThemeModeLight },
        { "dark", ThemeModeDark }
    };

    static const EnumString appSwitcherModeEnum[] =
    {
        { "app", AppSwitcherModeApp },
        { "window", AppSwitcherModeApp }
    };

#define GET_ENUM(ENTRY, DST, ENUM_STRING) \
if (TryGetEnum(lineBuf, #ENTRY, &DST, ENUM_STRING, sizeof(ENUM_STRING) / sizeof(ENUM_STRING[0])))\
    continue;

#define GET_BOOL(ENTRY, DST) \
if (TryGetBool(lineBuf,  #ENTRY, &DST))\
    continue;

#define GET_FLOAT(ENTRY, DST) \
if (TryGetFloat(lineBuf,  #ENTRY, &DST))\
    continue;

    static char lineBuf[1024];
    while (fgets(lineBuf, 1024, file))
    {
        if (!strncmp(lineBuf, "//", 2))
            continue;
        GET_ENUM("app hold key", config->_Key._AppHold, keyEnum)
        GET_ENUM("next app key", config->_Key._AppSwitch, keyEnum)
        GET_ENUM("previous app key", config->_Key._PrevApp, keyEnum)
        GET_ENUM("window hold key", config->_Key._WinHold, keyEnum)
        GET_ENUM("next window key", config->_Key._WinSwitch, keyEnum)
        GET_ENUM("next window key", config->_Key._WinSwitch, keyEnum)
        GET_ENUM("invert order key", config->_Key._Invert, keyEnum)
        GET_ENUM("theme", config->_ThemeMode, themeEnum)
        GET_ENUM("app switcher mode", config->_ThemeMode, appSwitcherModeEnum)

        GET_BOOL("allow mouse", config->_Mouse)
        GET_BOOL("check for updates", config->_CheckForUpdates)

        GET_FLOAT("scale", config->_Scale)
    }
    fclose(file);

#undef GET_ENUM
#undef GET_BOOL
#undef GET_FLOAT
}
