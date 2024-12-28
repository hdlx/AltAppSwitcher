#include <string.h>
#include <fileapi.h>
#include <dirent.h>
#include <ftw.h>
#include <windows.h>
#include "libzip/zip.h"
#include "Utils/File.h"
#include "Utils/GUI.h"

extern const unsigned char AASZip[];
extern const unsigned int SizeOfAASZip;

static void SetupGUI(GUIData* gui, void* userAppData)
{
    GridLayout(1, gui);
    CreateText("Test:", "", gui);

    GridLayout(4, gui);
    CreateText("Test:", "", gui);
    CreateText("Test:", "", gui);
    CreateText("Test:", "", gui);
    CreateText("Test:", "", gui);

    GridLayout(1, gui);
    CreateButton("Apply", (HMENU)1993, gui);
    CreateText("Test:", "", gui);
}

static void ButtonMessage(UINT buttonID, GUIData* guiData, void* appData)
{
    switch (buttonID)
    {
    case 0:
    {
    //
    }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    int appData;
    InitGUIWindow(SetupGUI, ButtonMessage, &appData, hInstance, "AASInstaller");
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    DeinitGUIWindow(hInstance, "AASInstaller");

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
