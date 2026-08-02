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

static void ApplyButton(void* userData)
{
    Config* cfg = (Config*)userData;
    WriteConfig(cfg);
    RestartAAS();
}

static void SetupGUI(gui_window_data* gui, void* userData)
{
    Config* cfg = (Config*)userData;

    LoadConfig(cfg);

    GridLayout(1, gui);
    SetBoldFont(gui);
    CreateText("Key bindings:", "", gui);

    GridLayout(2, gui);
    SetNormalFont(gui);

    CreateText("App hold", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppHold);

    CreateText("App switch", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppSwitch);

    CreateText("Win hold", "", gui);
    CreateKeyInputField(gui, &cfg->Key.WinHold);

    CreateText("Win switch", "", gui);
    CreateKeyInputField(gui, &cfg->Key.WinSwitch);

    CreateText("Invert", "", gui);
    CreateKeyInputField(gui, &cfg->Key.Invert);

    CreateText("Previous app", "", gui);
    CreateKeyInputField(gui, &cfg->Key.PrevApp);

    CreateText("App close", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppClose);

    GridLayout(1, gui);
    SetBoldFont(gui);
    CreateText("Graphic options:", "", gui);

    GridLayout(2, gui);
    SetNormalFont(gui);
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
    CreateText("Icons per row:", "0 for no limit (single row layout)", gui);
    CreateIntField("",
        &cfg->IconsPerRow, gui);

    GridLayout(1, gui);
    SetBoldFont(gui);
    CreateText("Other:", "", gui);

    GridLayout(2, gui);
    SetNormalFont(gui);
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
    CreateText("Ask for elevation", "", gui);
    CreateBoolControl("", &cfg->AskForElevation, gui);

    GridLayout(1, gui);
    CreateButton("Apply", gui, ApplyButton, (void*)cfg);
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
    GUIWindow(SetupGUI, (void*)&config, hInstance, title);
    return 0;
}