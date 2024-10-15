#include "App.h"
#include "CheckUpdate.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)nCmdShow; (void)pCmdLine;
    char version[64];
    GetVersionStr(version);
    return StartAltAppSwitcher(hInstance);
}
