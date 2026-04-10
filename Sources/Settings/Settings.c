#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <commctrl.h>
#include <stdio.h>
#include "Config/Config.h"
#include "Utils/GUI.h"
#include "Utils/Message.h"
#include "Utils/Version.h"
#include "Utils/Error.h"

#define APPLY_BUTTON_ID 1993

static void SetupGUI(GUIData* gui, void* userData)
{
    Config* cfg = (Config*)userData;

    LoadConfig(cfg);

    GridLayout(1, gui);
    CreateText("Key bindings:", "", gui);

    GridLayout(4, gui);
    CreateText("App hold", "", gui);
    CreateComboBox("", &cfg->Key.AppHold, keyES, gui);
    CreateText("App switch", "", gui);
    CreateComboBox("", &cfg->Key.AppSwitch, keyES, gui);
    CreateText("Win hold", "", gui);
    CreateComboBox("", &cfg->Key.WinHold, keyES, gui);
    CreateText("Win switch", "", gui);
    CreateComboBox("", &cfg->Key.WinSwitch, keyES, gui);
    CreateText("Invert", "", gui);
    CreateComboBox("", &cfg->Key.Invert, keyES, gui);
    CreateText("Previous app", "", gui);
    CreateComboBox("", &cfg->Key.PrevApp, keyES, gui);
    CreateText("App close", "", gui);
    CreateComboBox("", &cfg->Key.AppClose, keyES, gui);
    CreateText("", "", gui);
    CreateText("", "", gui);

    GridLayout(1, gui);
    CreateText("Graphic options:", "", gui);

    GridLayout(2, gui);
    CreateText("Theme:", "Color scheme. \"Auto\" to match system's.", gui);
    CreateComboBox("", &cfg->ThemeMode, themeES, gui);
    CreateText("Scale (\%):", "Scale controls icon size, expressed as percentage, 100 being Windows default icon size.", gui);
    CreatePercentField("",
        &cfg->Scale, gui);
    CreateText("Display app name:", "", gui);
    CreateComboBox("Display app name.", &cfg->DisplayName, displayNameES, gui);
    CreateText("Multiple monitor:", "Multiple monitor display mode.", gui);
    CreateComboBox("", &cfg->MultipleMonitorMode, multipleMonitorModeES, gui);
    CreateText("Restore minimized windows:", "", gui);
    CreateBoolControl("", &cfg->RestoreMinimizedWindows, gui);

    GridLayout(1, gui);
    CreateText("Other:", "", gui);

    GridLayout(2, gui);
    CreateText("Mouse:", "Allow selecting entry by clicking on the UI.", gui);
    CreateBoolControl("", &cfg->Mouse, gui);
    CreateText("Single selection tile:", "Mouse and keyboard use the same selection tile (MacOS-style)", gui);
    CreateBoolControl("", &cfg->MouseKbCommonSel, gui);
    CreateText("Check for updates:", "", gui);
    CreateBoolControl("", &cfg->CheckForUpdates, gui);
    CreateText("App switcher mode:", "App: MacOS-like, one entry per application.\nWindow: Windows-like, one entry per window (each window is considered an independent application)", gui);
    CreateComboBox("",
        &cfg->AppSwitcherMode, appSwitcherModeES, gui);
    CreateText("App filter mode:", "All: show apps from all monitors.\nmouse monitor: show only apps from the monitor where mouse cursor is located.", gui);
    CreateComboBox("",
        &cfg->AppFilterMode, appFilterModeES, gui);
    CreateText("Desktop filter:", "", gui);
    CreateComboBox("",
        &cfg->DesktopFilter, desktopFilterES, gui);

    CreateText("Debug only: disable icon focus", "", gui);
    CreateBoolControl("", &cfg->DebugDisableIconFocus, gui);

    GridLayout(1, gui);
    CreateButton("Apply", (HMENU)APPLY_BUTTON_ID, gui);
}

static void ButtonMessage(UINT buttonID, GUIData* guiData, void* userData)
{
    switch (buttonID) {
    case APPLY_BUTTON_ID: {
        Config* cfg = (Config*)userData;
        ApplyBindings(guiData);
        WriteConfig(cfg);
        RestartAAS();
    }
    default:
        break;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) // NOLINT
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;
    Config config = { };
    char title[260];
    int a = sprintf_s(title, sizeof(title), "AAS settings - v%u.%u", AAS_MAJOR, AAS_MINOR);
    ASSERT(a > 0);
    GUIWindow(SetupGUI, ButtonMessage, (void*)&config, hInstance, title);
    return 0;
}