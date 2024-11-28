#include "WinKeyCodes.h"

typedef struct EnumString
{
    char* Name;
    unsigned int Value;
} EnumString;

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