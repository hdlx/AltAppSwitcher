#include "Utils/Error.h"
#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <minwindef.h>
#include <libloaderapi.h>
#include <fileapi.h>
#include <handleapi.h>

static int DeleteForFtw(const char* path, const struct stat* data, int type, struct FTW* ftw)
{
    (void)data;
    (void)ftw;
    if (type == FTW_DP)
        rmdir(path);
    else
        unlink(path);
    return 0;
}

void StrBToF(char* str)
{
    while (*str++ != '\0') {
        if (*str == '\\')
            *str = '/';
    }
}

void WStrBToF(wchar_t* str)
{
    while (*str++ != L'\0') {
        if (*str == L'\\')
            *str = L'/';
    }
}

void StrFToB(char* str)
{
    while (*str++ != '\0') {
        if (*str == '/')
            *str = '\\';
    }
}

void DeleteTree(const char* dir)
{
    nftw(dir, DeleteForFtw, 0, FTW_DEPTH); // NOLINT
}

void ParentDir(const char* file, char* out)
{
    strcpy_s(out, MAX_PATH * sizeof(char), file);
    StrBToF(out);
    char* last = strrchr(out, '/');
    if (last)
        *last = '\0';
}

static void CopyFile(const char* srcStr, const char* dstStr)
{
    FILE* dst = fopen(dstStr, "wb");
    ASSERT(dst);
    if (!dst)
        return;
    FILE* src = fopen(srcStr, "rb");
    ASSERT(src);
    if (!src) {
        int a = fclose(dst);
        ASSERT(a == 0);
        return;
    }
    unsigned char buf[1024] = { };
    while (true) {
        int a = fseek(src, 0, SEEK_CUR);
        ASSERT(a == 0);
        size_t size = fread(buf, sizeof(char), sizeof(buf), src);
        if (size == 0)
            break;
        size_t b = fwrite(buf, sizeof(char), size, dst);
        ASSERT(b > 0);
    }
    int a = fclose(src);
    ASSERT(a == 0);
    a = fclose(dst);
    ASSERT(a == 0);
}

void CopyDirContent(const char* srcDir, const char* dstDir)
{
    char search[260] = { };
    strcpy_s(search, sizeof(search), srcDir);
    strcat_s(search, sizeof(search), "/*");
    WIN32_FIND_DATAA findData = { };
    HANDLE hFind = FindFirstFile(search, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        FindClose(hFind);
        return;
    }
    char srcFile[260] = { };
    char dstFile[260] = { };
    do {
        if (strcmp(findData.cFileName, "..") == 0 || strcmp(findData.cFileName, ".") == 0)
            continue;
        strcpy_s(srcFile, sizeof(srcFile), srcDir);
        strcat_s(srcFile, sizeof(srcFile), "/");
        strcat_s(srcFile, sizeof(srcFile), findData.cFileName);
        DWORD ftyp = GetFileAttributesA(srcFile);
        if (ftyp == INVALID_FILE_ATTRIBUTES)
            continue;
        if (ftyp & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        strcpy_s(dstFile, sizeof(dstFile), dstDir);
        StrBToF(dstFile);
        strcat_s(dstFile, sizeof(dstFile), "/");
        strcat_s(dstFile, sizeof(dstFile), findData.cFileName);
        CopyFile(srcFile, dstFile);
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}

void ConfigPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = { };
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat_s(outPath, sizeof(char) * MAX_PATH, "/AltAppSwitcherConfig.txt");
}

void LogPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = { };
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat_s(outPath, sizeof(char) * MAX_PATH, "/AltAppSwitcherLog.txt");
}

void UpdaterPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = { };
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat_s(outPath, sizeof(char) * MAX_PATH, "/Updater.exe");
}