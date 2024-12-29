#include <windows.h>
#include <Tlhelp32.h>
#include "Utils/MessageDef.h"

void RestartAAS()
{
    HANDLE procSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPPROCESS, (DWORD)0);
    PROCESSENTRY32 procEntry = {};
    procEntry.dwSize = sizeof(procEntry);
    BOOL procRes = Process32First(procSnap, &procEntry);
    while (procRes)
    {
        if (strcmp(procEntry.szExeFile, "AltAppSwitcher.exe"))
        {
            procRes = Process32Next(procSnap, &procEntry);
            continue;
        }
        {
            HANDLE threadSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPTHREAD, (DWORD)0);
            THREADENTRY32 threadEntry = {};
            threadEntry.dwSize = sizeof(threadEntry);
            BOOL threadRes = Thread32First(threadSnap, &threadEntry);
            while (threadRes)
            {
                if (procEntry.th32ProcessID == threadEntry.th32OwnerProcessID)
                    PostThreadMessage(threadEntry.th32ThreadID, MSG_RESTART_AAS, 0, 0);
                threadRes = Thread32Next(threadSnap, &threadEntry);
            }
            CloseHandle(threadSnap);
        }
        procRes = Process32Next(procSnap, &procEntry);
    }
    CloseHandle(procSnap);
    return;
}