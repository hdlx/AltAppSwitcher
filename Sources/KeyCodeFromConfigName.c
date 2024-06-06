#include "KeyCodeFromConfigName.h"
#include <stdint.h>
#include <windows.h>
#include <winuser.h>

typedef struct Pair
{
    char* Name;
    DWORD KeyCode;

} Pair;

// https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes

static Pair Pairs[] = {
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
};

static const uint32_t count = sizeof(Pairs) / sizeof(Pairs[0]);

DWORD KeyCodeFromConfigName(const char* p)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (strstr(p, Pairs[i].Name) != NULL)
            return Pairs[i].KeyCode;
    }
    return 0;
}