#pragma once
#include <windef.h>
#include <stdbool.h>

typedef struct KeyConfig
{
    unsigned int _AppHold;
    unsigned int _AppSwitch;
    unsigned int _WinHold;
    unsigned int _WinSwitch;
    unsigned int _Invert;
    unsigned int _PrevApp;
} KeyConfig;

typedef enum ThemeMode
{
    ThemeModeAuto,
    ThemeModeLight,
    ThemeModeDark
} ThemeMode;

typedef enum AppSwitcherMode
{
    AppSwitcherModeApp,
    AppSwitcherModeWindow,
} AppSwitcherMode;

typedef struct Config
{
    KeyConfig _Key;
    bool _Mouse;
    bool _CheckForUpdates;
    ThemeMode _ThemeMode;
    float _Scale;
    AppSwitcherMode _AppSwitcherMode;
} Config;

void LoadConfig(Config* config);
void WriteConfig(const Config* config);