
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>
#include <stdint.h>
#include <minwindef.h>
#include <time.h>
#include "Utils/File.h"
#include "Utils/Error.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

static void GetErrStr(DWORD err, char* str, uint32_t strSize)
{
    LPSTR msg = NULL;
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL);
    const uint32_t sizeToCopy = MIN(strlen(msg) + 1, strSize);
    memcpy(str, msg, sizeof(char) * sizeToCopy);
    str[strSize - 1] = '\0';
    LocalFree(msg);
}

void ASSError(const char* file, uint32_t line, const char* assertStr, bool crash)
{
     // Call GetLastError first, otherwise other winapi calls might overwrite last error.
    DWORD err = GetLastError();
    SetLastError(0);

    static char winMsg[512];
    memset(winMsg, '\0', sizeof(winMsg));
    if (err != 0)
    {
        GetErrStr(err, winMsg, 512);
    }

    time_t mytime = time(NULL);
    char* timeStr = ctime(&mytime);
    timeStr[strlen(timeStr) - 1] = '\0';

    char logFile[MAX_PATH] = {};
    LogPath(logFile);

    FILE* f = fopen(logFile, "ab");
    const char* type = crash ? "Assert" : "Verify";
    if (f != NULL)
    {
        fprintf(f, "%s:\nFile: %s, line: %u:\n", timeStr, file, line);
        fprintf(f, "%s: %s\n", type, assertStr);
        fprintf(f, "Last winapi error: %s\n\n", winMsg[0] == '\0' ? "None" : winMsg);
        fclose(f);

        printf("%s: %s\n", type, assertStr);
        printf("Last winapi error: %s\n\n", winMsg[0] == '\0' ? "None" : winMsg);
    }

    if (crash)
        DebugBreak();
}