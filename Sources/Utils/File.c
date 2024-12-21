#include <dirent.h>
#include <ftw.h>

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

void DeleteTree(const char* dir)
{
    nftw(dir, DeleteForFtw, 0, FTW_DEPTH);
}