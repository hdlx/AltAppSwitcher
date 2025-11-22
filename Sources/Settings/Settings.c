#include <windows.h>
#include <windowsx.h>
#include <wingdi.h>
#include <commctrl.h>
#include "Config/Config.h"
#include "Utils/GUI.h"
#include "Utils/Message.h"

#define APPLY_BUTTON_ID 1993

static void SetupGUI(GUIData* gui, void* userData)
{
    Config* cfg = (Config*)userData;

    LoadConfig(cfg);

    GridLayout(1, gui);
    CreateText("Key bindings:", "", gui);

    GridLayout(4, gui);
    CreateText("App hold", "", gui);
    CreateComboBox("", &cfg->_Key._AppHold, keyES, gui);
    CreateText("App switch", "", gui);
    CreateComboBox("", &cfg->_Key._AppSwitch, keyES, gui);
    CreateText("Win hold", "", gui);
    CreateComboBox("", &cfg->_Key._WinHold, keyES, gui);
    CreateText("Win switch", "", gui);
    CreateComboBox("", &cfg->_Key._WinSwitch, keyES, gui);
    CreateText("Invert", "", gui);
    CreateComboBox("", &cfg->_Key._Invert, keyES, gui);
    CreateText("Previous app", "", gui);
    CreateComboBox("", &cfg->_Key._PrevApp, keyES, gui);

    GridLayout(1, gui);
    CreateText("Graphic options:", "", gui);

    GridLayout(2, gui);
    CreateText("Theme:", "", gui);
    CreateComboBox("Color scheme. \"Auto\" to match system's.", &cfg->_ThemeMode, themeES, gui);
    CreateText("Scale (\%):", "", gui);
    CreatePercentField("Scale controls icon size, expressed as percentage, 100 being Windows default icon size.",
        &cfg->_Scale, gui);
    CreateText("Display app name:", "", gui);
    CreateComboBox("Display app name.", &cfg->_DisplayName, displayNameES, gui);
    CreateText("Multiple monitor:", "", gui);
    CreateComboBox("Multiple monitor display mode.", &cfg->_MultipleMonitorMode, multipleMonitorModeES, gui);
    CreateText("Restore minimized windows:", "", gui);
    CreateBoolControl("", &cfg->_RestoreMinimizedWindows, gui);

    GridLayout(1, gui);
    CreateText("Other:", "", gui);

    GridLayout(2, gui);
    CreateText("Mouse:", "", gui);
    CreateBoolControl("Allow selecting entry by clicking on the UI.", &cfg->_Mouse, gui);
    CreateText("Check for updates:", "", gui);
    CreateBoolControl("", &cfg->_CheckForUpdates, gui);
    CreateText("App switcher mode:", "", gui);
    CreateComboBox("App: MacOS-like, one entry per application.\nWindow: Windows-like, one entry per window (each window is considered an independent application)",
        &cfg->_AppSwitcherMode, appSwitcherModeES, gui);
    CreateText("App filter mode:", "", gui);
    CreateComboBox("All: show apps from all monitors.\nmouse monitor: show only apps from the monitor where mouse cursor is located.",
        &cfg->_AppFilterMode, appFilterModeES, gui);
    CreateText("Desktop filter:", "", gui);
    CreateComboBox("",
        &cfg->_DesktopFilter, desktopFilterES, gui);
    GridLayout(1, gui);
    CreateButton("Apply", (HMENU)APPLY_BUTTON_ID, gui);
}

static void ButtonMessage(UINT buttonID, GUIData* guiData, void* userData)
{
    switch (buttonID)
    {
    case APPLY_BUTTON_ID:
    {
        Config* cfg = (Config*)userData;
        ApplyBindings(guiData);
        WriteConfig(cfg);
        RestartAAS();
    }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    Config config = {};
    GUIWindow(SetupGUI, ButtonMessage, (void*)&config, hInstance, "AASSettings");
    return 0;
}