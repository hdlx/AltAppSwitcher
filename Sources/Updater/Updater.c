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
#include <sys/stat.h>
#include "libzip/zip.h"
#include "curl/curl/curl.h"
#include "cJSON/cJSON.h"
#include "Utils/File.h"
#include "Utils/Error.h"
#include "Utils/Version.h"
#include "Utils/Message.h"

typedef struct DynMem
{
  char* _Data;
  size_t _Size;
} DynMem;

static size_t writeData(void* ptr, size_t size, size_t nmemb, void* userData)
{
    (void)size;
    DynMem* mem = (DynMem*)userData;
    mem->_Data = realloc(mem->_Data, mem->_Size + nmemb);
    memcpy(mem->_Data + mem->_Size, ptr, nmemb);
    mem->_Size += nmemb;
    return nmemb;
}

static int GetAASVersion(BOOL preview, char* outVersion, char* outURL)
{
    CURL* curl = NULL;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl)
    {
        curl_global_cleanup();
        return 0;
    }

    DynMem response = {};
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/hdlx/altappswitcher/releases/latest");
    struct curl_slist *list = NULL;
    char userAgent[256] = {};
    sprintf(userAgent,  "User-Agent: AltAppSwitcher_v%i.%i", MAJOR, MINOR);
    list = curl_slist_append(list, userAgent);
    list = curl_slist_append(list, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    res = curl_easy_perform(curl);
    ASSERT(res == CURLE_OK)
    {
        response._Data = realloc(response._Data, response._Size + 1);
        response._Data[response._Size] = '\0';
        response._Size += 1;
    }
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    cJSON* json = cJSON_Parse(response._Data);
    free(response._Data);
    const cJSON* tagName = cJSON_GetObjectItem(json, "tag_name");
    const char* tag = cJSON_GetStringValue(tagName);
    int major = 0; int minor = 0;
    strcpy(outVersion, tag);
    sscanf(outVersion, "v%i.%i", &major, &minor);

    if (MAJOR >= major &&  MINOR >= minor)
    {
        cJSON_Delete(json);
        return 1;
    }

    const char arch[] =
#if defined(ARCH_x86_64)
       "x86_64";
#elif defined(ARCH_aarch64)
       "aarch64";
#else
#error
#endif

    const cJSON* assets = cJSON_GetObjectItem(json, "assets");
    for (int i = 0; i < cJSON_GetArraySize(assets); i++)
    {
        const cJSON* item = cJSON_GetArrayItem(assets, i);
        const cJSON* name = cJSON_GetObjectItem(item, "name");
        const char* nameStr = cJSON_GetStringValue(name);
        if (strstr(nameStr, arch) == NULL)
            continue;
        const cJSON* url = cJSON_GetObjectItem(item, "browser_download_url");
        strcpy(outURL, cJSON_GetStringValue(url));
    }
    cJSON_Delete(json);
    return 1;
}

static size_t CurlWriteFile(void* ptr, size_t size, size_t nmemb, void* userData)
{
    (void)size;
    FILE* f = (FILE*)userData;
    fwrite(ptr, 1, nmemb, f);
    return nmemb;
}

static void DownloadArchive(char* url, char* dstFile)
{
    CURL* curl = NULL;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl)
    {
        curl_global_cleanup();
        return;
    }

    FILE* file = fopen(dstFile,"wb");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    struct curl_slist *list = NULL;
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    res = curl_easy_perform(curl);
    ASSERT(res == CURLE_OK)
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    fclose(file);
}
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

    char version[64] = {};
    char archiveURL[512] = {};
    GetAASVersion(preview, version, archiveURL);
    if (archiveURL[0] == '\0')
        return 0;
    
    {
        char msg[256];
        sprintf(msg,
            "A new version of AltAppSwitcher is available (%s).\nDo you want to update now?",
            version);
        DWORD res = MessageBox(0, msg, "AltAppSwitcher updater", MB_YESNO);
        if (res == IDNO)
            return 0;
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
        DownloadArchive(archiveURL, archivePath);
    }

    // Copy updater to temp
    char updaterPath[256] = {};
    {
        char currentExe[256] = {};
        GetModuleFileName(NULL, currentExe, 256);
        char currentDir[256] = {};
        ParentDir(currentExe, currentDir);
        CopyDirContent(currentDir, tempDir);
        strcat(updaterPath, tempDir);
        strcat(updaterPath, "/Updater.exe");
    }

    // Run copied updater
    char args[512] = {};
    char AASDir[256] = {};
    GetCurrentDirectory(256, AASDir);
    sprintf(args, "--target \"%s\"", AASDir);
    ShellExecute(NULL, "runas", updaterPath, args, tempDir, SW_SHOWNORMAL);

    return 0;
}
