#include <string.h>
#include <fileapi.h>
#include <dirent.h>
#include <ftw.h>
#include <windows.h>
#include <shlobj.h>
#include "libzip/zip.h"
#include "Utils/File.h"
#include "Utils/GUI.h"

extern const unsigned char AASZip[];
extern const unsigned int SizeOfAASZip;

typedef struct AppData
{
    char _InstallPath[256];
    HWND _InstallPathText;
} AppData;

static void SetupGUI(GUIData* gui, void* userData)
{
    AppData* ad = (AppData*)userData;
    GridLayout(1, gui);
    SetBoldFont(gui);
    CreateText("Installation directory:", "", gui);
    SetNormalFont(gui);
    ad->_InstallPathText = CreateText(ad->_InstallPath, "", gui);
    CreateButton("Set directory", (HMENU)0, gui);
    WhiteSpace(gui);
    CreateButton("Install", (HMENU)1, gui);
}

static void ButtonMessage(UINT buttonID, GUIData* guiData, void* userData)
{
    AppData* ad = (AppData*)userData;
    switch (buttonID)
    {
    case 0:
    {
        BROWSEINFO bi = {};
        LPITEMIDLIST item = SHBrowseForFolder(&bi);
        SHGetPathFromIDList(item, ad->_InstallPath);
        SendMessage(ad->_InstallPathText, WM_SETTEXT, 0, (LPARAM)ad->_InstallPath);
        break;
    }
    case 1:
    {
        CloseGUI(guiData);
        break;
    }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    AppData appData = {};
    strcpy(appData._InstallPath, "default");
    GUIWindow(SetupGUI, ButtonMessage, &appData, hInstance, "AASInstaller");

    // Make temp dir
    char tempDir[256] = {};
    {
        GetTempPath(sizeof(tempDir), tempDir);
        StrBToF(tempDir);
        strcat(tempDir, "/AASInstaller");
        DIR* dir = opendir(tempDir);
        if (dir)
        {
            closedir(dir);
            DeleteTree(tempDir);
        }
        mkdir(tempDir);
    }

    char outZip[256] = {};
    strcat(outZip, tempDir);
    strcat(outZip, "/toto.zip");
    FILE* file = fopen(outZip,"wb");
    fwrite(AASZip, 1, SizeOfAASZip, file);
    fclose(file);
    return 0;
}
