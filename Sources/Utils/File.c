#include <dirent.h>
#include <ftw.h>
#include <stdio.h>
#include <dirent.h>
#include <minwindef.h>
#include <libloaderapi.h>

static int DeleteForFtw(const char* path, const struct stat* data, int type, struct FTW* ftw)
{
    (void)data; (void)ftw;
    if (type == FTW_DP)
        rmdir(path);
    else
        unlink(path);
    return 0;
}

void StrBToF(char* str)
{
    while (*str++ != '\0')
    {
        if (*str == '\\')
            *str = '/';
    }
}

void WStrBToF(wchar_t* str)
{
    while (*str++ != L'\0')
    {
        if (*str == L'\\')
            *str = L'/';
    }
}

void StrFToB(char* str)
{
    while (*str++ != '\0')
    {
        if (*str == '/')
            *str = '\\';
    }
}

void DeleteTree(const char* dir)
{
    nftw(dir, DeleteForFtw, 0, FTW_DEPTH);
}

void ParentDir(const char* file, char* out)
{
    strcpy(out, file);
    StrBToF(out);
    char* last = strrchr(out, '/');
    if (last)
        *last = '\0';
}

static void CopyFile(const char* srcStr, const char* dstStr)
{
    FILE* dst = fopen(dstStr, "wb");
    FILE* src = fopen(srcStr, "rb");
    unsigned char buf[1024];
    int size = 1;
    while (size)
    {
        size = fread(buf, sizeof(char), sizeof(buf), src);
        fwrite(buf, sizeof(char), size, dst);
    }
    fclose(src);
    fclose(dst);
}

void CopyDirContent(const char* srcDir, const char* dstDir)
{
    DIR* dir = opendir(srcDir);
    struct dirent* e = readdir(dir);
    while (e != NULL)
    {
        struct stat info;
        stat(e->d_name, &info);
        if (info.st_mode & S_IFDIR)
        {
        }
        else if (info.st_mode & S_IFREG)
        {
            char srcFile[256] = {};
            {
                strcpy(srcFile, srcDir);
                StrBToF(srcFile);
                strcat(srcFile, "/");
                strcat(srcFile, e->d_name);
            }
            char dstFile[256] = {};
            {
                strcpy(dstFile, dstDir);
                StrBToF(dstFile);
                strcat(dstFile, "/");
                strcat(dstFile, e->d_name);
            }
            CopyFile(srcFile, dstFile);
        }
        e = readdir(dir);
    }
    closedir(dir);
}

void ConfigPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = {};
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat(outPath, "/AltAppSwitcherConfig.txt");
}

void LogPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = {};
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat(outPath, "/AltAppSwitcherLog.txt");
}

void UpdaterPath(char* outPath)
{
    outPath[0] = '\0';
    char currentExe[MAX_PATH] = {};
    GetModuleFileName(NULL, currentExe, MAX_PATH);
    ParentDir(currentExe, outPath);
    strcat(outPath, "/Updater.exe");
}