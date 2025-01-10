#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fileapi.h>
#include <dirent.h>
#include <windef.h>
#include <processthreadsapi.h>
#include <winbase.h>
#include <winuser.h>
#include <shellapi.h>
#include "libzip/zip.h"
#include "curl/curl/curl.h"
#include "Utils/File.h"
#include "Utils/Error.h"
#include "Utils/Version.h"
#include "Utils/Message.h"

static void GetAASVersion(int* major, int* minor, BOOL preview)
{
    *major = 0;
    *minor = 0;
    const char message[] =
       "GET /aasversion HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n";
    (void)message;
    //if (SOCKET_ERROR == send(sock, message, strlen(message), 0))
    //    return;

    char response[1024];
    memset(response, 0, sizeof(response));
   //if (SOCKET_ERROR == recv(sock, response, sizeof(response), 0))
   //    return;

    if (preview)
    {
        const char version[] = "\"PreviewVersion\": ";
        char* at = strstr(response, version);
        if (at == NULL)
            return;
        sscanf(at, "\"PreviewVersion\": %i.%i", major, minor);
    }
    else
    {
        const char version[] = "\"Version\": ";
        char* at = strstr(response, version);
        if (at == NULL)
            return;
        sscanf(at, "\"Version\": %i.%i", major, minor);
    }
    return;
}
/*
static void DownloadArchive(SOCKET sock, int major, int minor, const char* dstFile)
{
    const char arch[] =
#if defined(ARCH_x86_64)
       "x86_64";
#elif defined(ARCH_aarch64)
       "aarch64";
#else
#error
#endif

    char msg[512] = {};
    sprintf(msg, "GET /aasarchive-%i-%i/AltAppSwitcher_%s.zip HTTP/1.1\r\nHost: www.hamtarodeluxe.com\r\n\r\n", major, minor, arch);

    if (SOCKET_ERROR == send(sock, msg, strlen(msg), 0))
    {
        close(sock);
        WSACleanup();
        return;
    }

    int fileSize = 0;
    {
        char buf[1024];
        memset(buf, '\0', sizeof(buf));
        char* p = buf + 4;
        while (1)
        {
            if (SOCKET_ERROR == recv(sock, p, 1, 0))
                return;
            if (!strncmp(p - 3, "\r\n\r\n", 4))
                break;
            p++;
        }
        p = buf + 4;
        // printf("%s", p);
        ASSERT(strstr(p, "404 Not Found") == NULL);
        char* at = strstr(p, "content-length");
        if (at == NULL)
            return;
        const int ret = sscanf(at, "content-length: %i", &fileSize);
        ASSERT(ret != -1);
        ASSERT(fileSize > 0)
    }

    int bytes = 0;
    FILE* file = fopen(dstFile,"wb");
    while (1)
    {
        static char response[1024];
        memset(response, 0, sizeof(response));
        const int bytesRecv = recv(sock, response, 1024, 0);
        if (bytesRecv == -1)
            return;
        fwrite(response, 1, bytesRecv, file);
        bytes += bytesRecv;
        if (bytes == fileSize)
            break;
    }
    fclose(file);
}
*/
static void Extract(const char* targetDir)
{
    CloseAASBlocking();
    int err = 0;
    struct zip* z = zip_open("./AltAppSwitcher.zip" , 0, &err);
    unsigned char buf[1024];
    for (int i = 0; i < zip_get_num_entries(z, 0); i++)
    {
        struct zip_stat zs = {};
        zip_stat_index(z, i, 0, &zs);
        printf("Name: [%s], ", zs.name);
        if (!strcmp(zs.name, "AltAppSwitcherConfig.txt"))
            continue;
        struct zip_file* zf = zip_fopen_index(z, i, 0);
        char dstPath[256] = {};
        strcpy(dstPath, targetDir);
        strcat(dstPath, "/");
        strcat(dstPath, zs.name);
        FILE* dstFile = fopen(dstPath, "wb");
        int sum = 0;
        while (sum != zs.size)
        {
            int len = zip_fread(zf, buf, sizeof(buf));
            fwrite(buf, sizeof(unsigned char),len, dstFile);
            sum += len;
        }
        fclose(dstFile);
        zip_fclose(zf);
    }
    {
        char AASExe[256] = {};
        strcat(AASExe, targetDir);
        strcat(AASExe, "/AltAppSwitcher.exe");
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};
        CreateProcess(NULL, AASExe, 0, 0, 0, CREATE_NEW_PROCESS_GROUP, 0, targetDir, &si, &pi);
    }
    MessageBox(0, "AltAppSwitcher successfully updated", "AltAppSwitcher", MB_OK | MB_SETFOREGROUND);
}

static size_t writeData(void* ptr, size_t size, size_t nmemb, void* userData)
{
    (void)nmemb;
    (char*)* use
    memcpy(ptr, userData, size);
    (userData) += size;
    return size;
}

int main(int argc, char *argv[])
{
    BOOL extract = 0;
    BOOL preview = 0;
    char targetDir[256] = {};
    for (int i = 0; i < argc; i++)
    {
        if (!strcmp(argv[i], "--target") && (i + 1) < argc)
        {
            strcpy(targetDir, argv[i + 1]);
            i++;
            extract = 1;
        }
        else if (!strcmp(argv[i], "--preview"))
        {
            preview = 1;
        }
    }
    if (extract)
    {
        Extract(targetDir);
        return 0;
    }
    CURL* curl = NULL;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl)
    {
        curl_global_cleanup();
        return 0;
    }

    char response[1024] = {};

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/hdlx/altappswitcher/releases/latest/url");
    struct curl_slist *list = NULL;
    list = curl_slist_append(list, "User-Agent: Awesome-Octocat-App");
    list = curl_slist_append(list, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    res = curl_easy_perform(curl);
    ASSERT(res == CURLE_OK)
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    
    int major, minor;
    GetAASVersion(&major, &minor, preview);
    if ((MAJOR >= major) && (MINOR >= minor))
    {
        return 0;
    }
    {
        char msg[256];
        sprintf(msg,
            "A new version of AltAppSwitcher is available (%u.%u).\nDo you want to update now?",
            major, minor);
        DWORD res = MessageBox(0, msg, "AltAppSwitcher updater", MB_YESNO);
        if (res == IDNO)
        {
            return 0;
        }
    }

    // Make temp dir
    char tempDir[256] = {};
    {
        GetTempPath(sizeof(tempDir), tempDir);
        StrBToF(tempDir);
        strcat(tempDir, "AASUpdater");
        DIR* dir = opendir(tempDir);
        if (dir)
        {
            closedir(dir);
            DeleteTree(tempDir);
        }
        mkdir(tempDir);
    }

    char archivePath[256] = {};
    {
        strcpy(archivePath, tempDir);
        strcat(archivePath, "/AltAppSwitcher.zip");
   //     DownloadArchive(sock, major, minor, archivePath);
    }

    // Copy updater to temp
    char updaterPath[256] = {};
    {
        strcpy(updaterPath, tempDir);
        strcat(updaterPath, "/Updater.exe");
        char currentExe[256];
        GetModuleFileName(NULL, currentExe, 256);
        FILE* dst = fopen(updaterPath, "wb");
        FILE* src = fopen(currentExe, "rb");
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

    // Run copied updater
    char args[512] = {};
    char AASDir[256] = {};
    GetCurrentDirectory(256, AASDir);
    sprintf(args, "--target \"%s\"", AASDir);
    ShellExecute(NULL, "runas", updaterPath, args, tempDir, SW_SHOWNORMAL);

    return 0;
}
