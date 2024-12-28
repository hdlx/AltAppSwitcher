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
    HWND _InstallPathWidget;
    bool _Install;
} AppData;

static void SetupGUI(GUIData* gui, void* userData)
{
    AppData* ad = (AppData*)userData;
    GridLayout(1, gui);
    SetBoldFont(gui);
    CreateText("Installation directory:", "", gui);
    SetNormalFont(gui);
    ad->_InstallPathWidget = CreateText(ad->_InstallPath, "", gui);
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
        if (item == NULL)
            break;
        SHGetPathFromIDList(item, ad->_InstallPath);
        strcat(ad->_InstallPath, "\\AltAppSwitcher");
        SendMessage(ad->_InstallPathWidget, WM_SETTEXT, 0, (LPARAM)ad->_InstallPath);
        break;
    }
    case 1:
    {
        CloseGUI(guiData);
        ad->_Install = true;
        break;
    }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    AppData appData = {};
    SHGetSpecialFolderPath(
            0,
            appData._InstallPath,
            CSIDL_PROGRAM_FILES,
            FALSE);
    strcat(appData._InstallPath, "\\AltAppSwitcher");
    GUIWindow(SetupGUI, ButtonMessage, &appData, hInstance, "AASInstaller");

    if (!appData._Install)
        return 0;

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

    // Write zip to disk
    char outZip[256] = {};
    {
        strcat(outZip, tempDir);
        strcat(outZip, "/toto.zip");
        FILE* file = fopen(outZip,"wb");
        fwrite(AASZip, 1, SizeOfAASZip, file);
        fclose(file);
    }

    DIR* dir = opendir(appData._InstallPath);
    if (!dir)
        mkdir(appData._InstallPath);
    else
        closedir(dir);

    {
        int err = 0;
        struct zip* z = zip_open(outZip, 0, &err);
        unsigned char buf[1024];
        for (int i = 0; i < zip_get_num_entries(z, 0); i++)
        {
            struct zip_stat zs = {};
            zip_stat_index(z, i, 0, &zs);
            printf("Name: [%s], ", zs.name);
            struct zip_file* zf = zip_fopen_index(z, i, 0);
            char dstPath[256] = {};
            strcpy(dstPath, appData._InstallPath);
            strcat(dstPath, "/");
            strcat(dstPath, zs.name);
            FILE* dstFile = fopen(dstPath, "wb");
            int sum = 0;
            while (sum != zs.size)
            {
                int len = zip_fread(zf, buf, sizeof(buf));
                fwrite(buf, sizeof(unsigned char),len, dstFile);
                sum += len;
            }
            fclose(dstFile);
            zip_fclose(zf);
        }
    }

    return 0;
}
