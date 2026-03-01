#pragma once
#include <windef.h>
#include <stdbool.h>

struct KeyConfig {
    unsigned int AppHold;
    unsigned int AppSwitch;
    unsigned int WinHold;
    unsigned int WinSwitch;
    unsigned int Invert;
    unsigned int PrevApp;
    unsigned int AppClose;
};

typedef enum ThemeMode {
    ThemeModeAuto,
    ThemeModeLight,
    ThemeModeDark
} ThemeMode;

typedef enum DisplayName {
    DisplayNameSel,
    DisplayNameAll,
    DisplayNameNone
} DisplayName;

typedef enum AppSwitcherMode {
    AppSwitcherModeApp,
    AppSwitcherModeWindow,
} AppSwitcherMode;

typedef enum MultipleMonitorMode {
    MultipleMonitorModeMouse,
    MultipleMonitorModeMain,
} MultipleMonitorMode;

typedef enum AppFilterMode {
    AppFilterModeAll,
    AppFilterModeMouseMonitor,
} AppFilterMode;

typedef enum DesktopFilter {
    DesktopFilterCurrent,
    DesktopFilterAll,
} DesktopFilter;

typedef struct Config {
    struct KeyConfig Key;
    bool Mouse;
    bool CheckForUpdates;
    ThemeMode ThemeMode;
    DisplayName DisplayName;
    float Scale;
    AppSwitcherMode AppSwitcherMode;
    MultipleMonitorMode MultipleMonitorMode;
    AppFilterMode AppFilterMode;
    bool RestoreMinimizedWindows;
    DesktopFilter DesktopFilter;
    bool DebugDisableIconFocus;
} Config;

typedef struct EnumString {
    const char* Name;
    unsigned int Value;
} EnumString;

extern const EnumString keyES[17];
extern const EnumString themeES[4];
extern const EnumString appSwitcherModeES[3];
extern const EnumString displayNameES[4];
extern const EnumString multipleMonitorModeES[3];
extern const EnumString appFilterModeES[3];
extern const EnumString desktopFilterES[3];

void LoadConfig(Config* config);
void WriteConfig(const Config* config);
void DefaultConfig(Config* config);