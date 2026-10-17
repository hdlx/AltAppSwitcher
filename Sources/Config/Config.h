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
    DisplayNameNone,
    DisplayNameSelFull
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
    bool MouseKbCommonSel;
    bool CheckForUpdates;
    ThemeMode ThemeMode;
    DisplayName DisplayName;
    float Scale;
    AppSwitcherMode AppSwitcherMode;
    MultipleMonitorMode MultipleMonitorMode;
    AppFilterMode AppFilterMode;
    bool RestoreMinimizedWindows;
    DesktopFilter DesktopFilter;
    int IconsPerRow;
    bool AskForElevation;
    int NameLines; // Max number of text lines for the name under each icon.
    float TileWidth; // Tile width relative to icon size.
    int NameFontSize; // Name font size in pixels, 0 for automatic (relative to icon size).
    bool NameSystemFont; // Use the system UI font for names instead of Segoe UI.
} Config;

typedef struct EnumString {
    const char* ValStr;
    unsigned int ValUInt;
} EnumString;

extern const EnumString keyES[17];
extern const EnumString themeES[4];
extern const EnumString appSwitcherModeES[3];
extern const EnumString displayNameES[5];
extern const EnumString multipleMonitorModeES[3];
extern const EnumString appFilterModeES[3];
extern const EnumString desktopFilterES[3];

void LoadConfig(Config* config);
void WriteConfig(const Config* config);
void DefaultConfig(Config* config);