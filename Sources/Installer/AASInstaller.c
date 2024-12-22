#include <string.h>
#include <fileapi.h>
#include <dirent.h>
#include <ftw.h>
#include "libzip/zip.h"
#include "Utils/File.h"

extern const unsigned char AASZip[];
extern const unsigned int SizeOfAASZip;

int main(int argc, char** argv)
{
    // Make temp dir
    char tempDir[256] = {};
    {
        GetTempPath(sizeof(tempDir), tempDir);
        StrBToF(tempDir);
        strcat(tempDir, "/AASInstaller");
        DIR* dir = opendir(tempDir);
        if (dir)
        {
            closedir(dir);
            DeleteTree(tempDir);
        }
        mkdir(tempDir);
    }

    char outZip[256] = {};
    strcat(outZip, tempDir);
    strcat(outZip, "/toto.zip");
    FILE* file = fopen(outZip,"wb");
    fwrite(AASZip, 1, SizeOfAASZip, file);
    fclose(file);
    return 0;
}
