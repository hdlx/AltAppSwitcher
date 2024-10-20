#include "App.h"
#include "CheckUpdate.h"

#define MAJOR 0
#define MINOR 17
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance; (void)nCmdShow; (void)pCmdLine;
    int major, minor;
    GetAASVersion(&major, &minor);
    if ((major > MAJOR) ||
        (major == MAJOR && minor > MINOR))
    {
        system("msg * \"A new version of AltAppSwitcher is available. Please check https://github.com/hdlx/AltAppSwitcher/releases");
    }
    return StartAltAppSwitcher(hInstance);
}
