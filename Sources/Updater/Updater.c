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

typedef struct DynMem {
    char* Data;
    size_t Size;
} DynMem;

static size_t writeData(void* ptr, size_t size, size_t nmemb, void* userData)
{
    (void)size;
    DynMem* mem = (DynMem*)userData;
    void* old = mem->Data;
    mem->Data = realloc(old, mem->Size + nmemb);
    ASSERT(mem->Data)
    if (!mem->Data) {
        free(old);
        return 0;
    }
    memcpy_s(mem->Data + mem->Size, nmemb, ptr, nmemb);
    mem->Size += nmemb;
    return nmemb;
}

static int GetLastAASVersion(BOOL preview, char* outVersion, char* assetURL)
{
    CURL* curl = NULL;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        return 0;
    }

    DynMem response = {};
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.github.com/repos/hdlx/altappswitcher/releases");
    struct curl_slist* list = NULL;
    char userAgent[256] = {};
    int a = sprintf_s(userAgent, sizeof(userAgent) / sizeof(userAgent[0]), "User-Agent: AltAppSwitcher_v%i.%i", AAS_MAJOR, AAS_MINOR);
    ASSERT(a > 0);
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
        void* old = response.Data;
        response.Data = realloc(old, response.Size + 1);
        if (!response.Data) {
            free(old);
            ASSERT(true);
            return 0;
        }
        response.Data[response.Size] = '\0';
        response.Size += 1;
    }
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    cJSON* json = cJSON_Parse(response.Data);
    free(response.Data);

    const cJSON* release = NULL;
    for (int i = 0; i < cJSON_GetArraySize(json); i++) {
        release = cJSON_GetArrayItem(json, i);
        const cJSON* preRelease = cJSON_GetObjectItem(release, "prerelease");
        if (preview == cJSON_IsTrue(preRelease))
            break;
    }

    const cJSON* tagName = cJSON_GetObjectItem(release, "tag_name");
    const char* tag = cJSON_GetStringValue(tagName);
    int major = 0;
    int minor = 0;
    strcpy_s(outVersion, sizeof(char) * 64, tag);
    int x = sscanf_s(outVersion, "v%i.%i", &major, &minor);
    ASSERT(x > 0);

    if (AAS_MAJOR >= major && AAS_MINOR >= minor) {
        cJSON_Delete(json);
        return 1;
    }

    const char arch[] =
#ifdef ARCH_x86_64
        "x86_64";
#elif defined(ARCH_aarch64)
        "aarch64";
#else
#error
#endif

    const cJSON* assets = cJSON_GetObjectItem(release, "assets");
    for (int i = 0; i < cJSON_GetArraySize(assets); i++) {
        const cJSON* asset = cJSON_GetArrayItem(assets, i);
        const cJSON* name = cJSON_GetObjectItem(asset, "name");
        const char* nameStr = cJSON_GetStringValue(name);
        if (strstr(nameStr, arch) == NULL)
            continue;
        strcpy_s(assetURL, sizeof(char) * 512, cJSON_GetStringValue(cJSON_GetObjectItem(asset, "url")));
    }
    cJSON_Delete(json);
    return 1;
}

static size_t CurlWriteFile(void* ptr, size_t size, size_t nmemb, void* userData)
{
    (void)size;
    FILE* f = (FILE*)userData;
    unsigned long long a = fwrite(ptr, 1, nmemb, f);
    ASSERT(a > 0);
    return nmemb;
}

static void DownloadArchive(const char* dstFile, const char* url)
{
    CURL* curl = NULL;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) {
        curl_global_cleanup();
        return;
    }

    FILE* file = fopen(dstFile, "wb");
    ASSERT(file);
    if (!file)
        return;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    struct curl_slist* list = NULL;
    char userAgent[256] = {};
    int a = sprintf_s(userAgent, sizeof(userAgent) / sizeof(userAgent[0]), "User-Agent: AltAppSwitcher_v%i.%i", AAS_MAJOR, AAS_MINOR);
    ASSERT(a > 0);
    list = curl_slist_append(list, userAgent);
    // list = curl_slist_append(list, "Accept: application/vnd.github+json");
    list = curl_slist_append(list, "Accept: application/octet-stream");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    res = curl_easy_perform(curl);
    ASSERT(res == CURLE_OK)
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    int x = fclose(file);
    ASSERT(x == 0);
}

static void Extract(const char* targetDir)
{
    CloseAASBlocking();
    int err = 0;
    struct zip* z = zip_open("./AltAppSwitcher.zip", 0, &err);
    ASSERT(z);
    if (!z)
        return;
    unsigned char buf[1024];
    for (int i = 0; i < zip_get_num_entries(z, 0); i++) {
        struct zip_stat zs = {};
        zip_stat_index(z, i, 0, &zs);
        // printf("Name: [%s], ", zs.name);
        if (!strcmp(zs.name, "AltAppSwitcherConfig.txt"))
            continue;
        struct zip_file* zf = zip_fopen_index(z, i, 0);
        ASSERT(zf);
        if (!zf)
            return;
        char dstPath[256] = {};
        strcpy_s(dstPath, sizeof(dstPath), targetDir);
        strcat_s(dstPath, sizeof(dstPath), "/");
        strcat_s(dstPath, sizeof(dstPath), zs.name);
        FILE* dstFile = fopen(dstPath, "wb");
        ASSERT(dstFile);
        if (!dstFile)
            return;
        long long sum = 0;
        while (sum != zs.size) {
            long long len = zip_fread(zf, buf, sizeof(buf));
            unsigned long long a = fwrite(buf, sizeof(unsigned char), len, dstFile);
            ASSERT(a > 0);
            sum += len;
        }
        int a = fclose(dstFile);
        ASSERT(a == 0);
        zip_fclose(zf);
    }
    {
        char AASExe[256] = {};
        strcat_s(AASExe, sizeof(AASExe), targetDir);
        strcat_s(AASExe, sizeof(AASExe), "/AltAppSwitcher.exe");
        STARTUPINFO si = {};
        PROCESS_INFORMATION pi = {};
        CreateProcess(NULL, AASExe, 0, 0, 0, CREATE_NEW_PROCESS_GROUP, 0, targetDir, &si, &pi);
    }
    MessageBox(0, "AltAppSwitcher successfully updated", "AltAppSwitcher", MB_OK | MB_SETFOREGROUND);
}

int main(int argc, char* argv[])
{
    BOOL extract = 0;
    BOOL preview = 0;
    char targetDir[256] = {};
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--target") && (i + 1) < argc) {
            strcpy_s(targetDir, sizeof(targetDir), argv[i + 1]);
            i++;
            extract = 1;
        } else if (!strcmp(argv[i], "--preview")) {
            preview = 1;
        }
    }
    if (extract) {
        Extract(targetDir);
        return 0;
    }

    char version[64] = {};
    char assetURL[512] = {};
    GetLastAASVersion(preview, version, assetURL);
    if (assetURL[0] == '\0')
        return 0;

    {
        char msg[256];
        int a = sprintf_s(
            msg,
            sizeof(msg) / sizeof(msg[0]),
            "A new version of AltAppSwitcher is available (%s).\nDo you want to update now?",
            version);
        ASSERT(a > 0);
        DWORD res = MessageBox(0, msg, "AltAppSwitcher updater", MB_YESNO);
        if (res == IDNO)
            return 0;
    }

    // Make temp dir
    char tempDir[256] = {};
    {
        GetTempPath(sizeof(tempDir), tempDir);
        StrBToF(tempDir);
        strcat_s(tempDir, sizeof(tempDir), "AASUpdater");
        DIR* dir = opendir(tempDir);
        if (dir) {
            closedir(dir);
            DeleteTree(tempDir);
        }
        mkdir(tempDir);
    }

    char archivePath[256] = {};
    {
        strcpy_s(archivePath, sizeof(archivePath), tempDir);
        strcat_s(archivePath, sizeof(archivePath), "/AltAppSwitcher.zip");
        DownloadArchive(archivePath, assetURL);
    }

    // Copy updater to temp
    char updaterPath[256] = {};
    {
        char currentExe[256] = {};
        GetModuleFileName(NULL, currentExe, 256);
        char currentDir[256] = {};
        ParentDir(currentExe, currentDir);
        CopyDirContent(currentDir, tempDir);
        strcat_s(updaterPath, sizeof(updaterPath), tempDir);
        strcat_s(updaterPath, sizeof(updaterPath), "/Updater.exe");
    }

    // Run copied updater
    char args[512] = {};
    char AASDir[256] = {};
    GetCurrentDirectory(256, AASDir);
    int a = sprintf_s(args, sizeof(args) / sizeof(args[0]), "--target \"%s\"", AASDir);
    ASSERT(a > 0);
    ShellExecute(NULL, "runas", updaterPath, args, tempDir, SW_SHOWNORMAL);

    return 0;
}
