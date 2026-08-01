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

    GridLayout(3, gui);

    CreateText("App hold", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppHold);
    CreateText("Dummy name", "", gui);

    CreateText("App switch", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppSwitch);
    CreateText("Dummy name", "", gui);

    CreateText("Win hold", "", gui);
    CreateKeyInputField(gui, &cfg->Key.WinHold);
    CreateText("Dummy name", "", gui);

    CreateText("Win switch", "", gui);
    CreateKeyInputField(gui, &cfg->Key.WinSwitch);
    CreateText("Dummy name", "", gui);

    CreateText("Invert", "", gui);
    CreateKeyInputField(gui, &cfg->Key.Invert);
    CreateText("Dummy name", "", gui);

    CreateText("Previous app", "", gui);
    CreateKeyInputField(gui, &cfg->Key.PrevApp);
    CreateText("Dummy name", "", gui);

    CreateText("App close", "", gui);
    CreateKeyInputField(gui, &cfg->Key.AppClose);
    CreateText("Dummy name", "", gui);

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
    CreateText("Icons per row:", "0 for no limit (single row layout)", gui);
    CreateIntField("",
        &cfg->IconsPerRow, gui);

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