#include <psdk_inc/_ip_types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fileapi.h>
#include <dirent.h>
#include <ftw.h>
#include "libzip/zip.h"

/*
int DeleteForFtw(const char* path, const struct stat* data, int type, struct FTW* ftw)
{
    (void)data; (void)ftw;
    if (type == FTW_DP)
        rmdir(path);
    else // if (type == FTW_F)
        unlink(path);
    return 0;
}

static void StrBToF(char* str)
{
    while (*str++ != '\0')
    {
        if (*str == '\\')
            *str = '/';
    }
}

static void ExtractArchive(const char* inPath)
{
    //Open the ZIP archive
    int err = 0;
    struct zip *za = zip_open(inPath, 0, &err);

    int a = zip_get_num_entries(za, 0);
    for (int i = 0; i < a; i++)
    {
        struct zip_stat sb = {};
        zip_stat_index(za, i, 0, &sb);
        printf("Name: [%s], ", sb.name);
    }

    //Read the compressed file
    //struct zip_file *f = zip_fopen(z, name, 0);
    //zip_fread(f, contents, st.size);
    //zip_fclose(f);

    zip_close(za);
    return;
}
*/

int main()
{
    // Make temp dir
    /*
    char tempDir[256] = {};
    {
        StrBToF(tempDir);
        strcat(tempDir, "AltAppSwitcher");
        DIR* dir = opendir(tempDir);
        if (dir)
        {
            closedir(dir);
            nftw(tempDir, DeleteForFtw, 0, FTW_DEPTH);
        }
        mkdir(tempDir);
    }

    char archivePath[256] = {};
    strcpy(archivePath, tempDir);
    strcat(archivePath, "/aas.zip");
    DownloadLatest(sock, archivePath);

    close(sock);
    WSACleanup();

    ExtractArchive(archivePath);*/
    printf("Test");
    return 0;
}
