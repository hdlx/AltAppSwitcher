#include <windows.h>
#include <Tlhelp32.h>
#include "Utils/MessageDef.h"

static void PostAASMsg(int msg)
{
    HANDLE procSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPPROCESS, (DWORD)0);
    PROCESSENTRY32 procEntry = {};
    procEntry.dwSize = sizeof(procEntry);
    BOOL procRes = Process32First(procSnap, &procEntry);
    while (procRes)
    {
        if (0 != strcmp(procEntry.szExeFile, "AltAppSwitcher.exe"))
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
                    PostThreadMessage(threadEntry.th32ThreadID, msg, 0, 0);
                threadRes = Thread32Next(threadSnap, &threadEntry);
            }
            CloseHandle(threadSnap);
        }
        procRes = Process32Next(procSnap, &procEntry);
    }
    CloseHandle(procSnap);
}

void RestartAAS()
{
    PostAASMsg(MSG_RESTART_AAS);
}

void CloseAAS()
{
    PostAASMsg(MSG_CLOSE_AAS);
}

int AASIsRunning()
{
    HANDLE procSnap = CreateToolhelp32Snapshot((DWORD)TH32CS_SNAPPROCESS, (DWORD)0);
    PROCESSENTRY32 procEntry = {};
    procEntry.dwSize = sizeof(procEntry);
    BOOL procRes = Process32First(procSnap, &procEntry);
    while (procRes)
    {
        if (!strcmp(procEntry.szExeFile, "AltAppSwitcher.exe"))
        {
            CloseHandle(procSnap);
            return 1;
        }
        procRes = Process32Next(procSnap, &procEntry);
    }
    CloseHandle(procSnap);
    return 0;
}

void CloseAASBlocking()
{
    PostAASMsg(MSG_CLOSE_AAS);
    while (AASIsRunning())
    {}
}