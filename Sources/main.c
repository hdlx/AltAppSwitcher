#include "App.h"
#include "CheckUpdate.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)nCmdShow; (void)pCmdLine;
    int major, minor;
    GetAASVersion(&major, &minor);
    return StartAltAppSwitcher(hInstance);
}
