#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <minwindef.h>
#include <time.h>

#define ASSERT(arg) if (!(arg)) { MSSError(__FILE__, __LINE__, #arg); }

static void GetLastWinErrStr(char* str, uint32_t strSize)
{
    DWORD err = GetLastError();
    if (err == 0)
        return;
    LPSTR msg = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL);
    const uint32_t sizeToCopy = min(strlen(msg) + 1, strSize);
    memcpy(str, msg, sizeof(char) * sizeToCopy);
    str[strSize - 1] = '\0';
    LocalFree(msg);
}

static void PrintLastError()
{
    static char msg[512];
    GetLastWinErrStr(msg, 512);
    printf("%s", msg);
}

static void GetCurrentProcessName(char* processName, uint32_t strMaxSize)
{
    HMODULE module[1] = {};
    DWORD sizeNeeded;
    if (EnumProcessModules(GetCurrentProcess(), module, sizeof(module), &sizeNeeded))
        GetModuleBaseName(GetCurrentProcess(), module[0], processName, strMaxSize);
}

static void Lowercase(char* str)
{
    for (int i = 0; str[i]; i++)
        str[i] = tolower(str[i]);
}

static void MSSError(const char* file, uint32_t line, const char* assertStr)
{
    time_t mytime = time(NULL);
    char* timeStr = ctime(&mytime);
    timeStr[strlen(timeStr) - 1] = '\0';
    static char winMsg[512];
    GetLastWinErrStr(winMsg, 512);

    FILE* f = fopen("./AltAppSwitcherLog.txt", "ab");
    if (f == NULL)
        return;
    fprintf(f, "%s:\nFile: %s, line: %u:\n", timeStr, file, line);
    fprintf(f, "Assert: %s\n", assertStr);
    fprintf(f, "Last winapi error: %s\n\n", winMsg[0] == '\0' ? "None" : winMsg);
    fclose(f);

    SetLastError(0);

    DebugBreak();
}

static void MyPrintWindow(HWND win)
{
    printf("\n");
    static char buf[512];
    GetClassName(win, buf, 100);
    printf("    CLASS: %s \n", buf);
    GetWindowText(win, buf, 100);
    printf("   TEXT: %s \n", buf);
    DWORD dwPID = 0x0000000000000000;
    ASSERT(GetWindowThreadProcessId(win, &dwPID));
    printf("    PID: %i \n", (int)dwPID);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, dwPID);
    ASSERT(process);
    static char pathStr[512];
    GetModuleFileNameEx(process, NULL, pathStr, 512);
    CloseHandle(process);
    printf("    Filename: %s \n", pathStr);
}