#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>

#define MAX_FD 20

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

int files = 0, dirs = 0, links = 0, others = 0;

int fn(const char* path, const struct stat* fileStat, int type, struct FTW* ftwStruct) {
    switch (type) {
        case FTW_DNR:
        case FTW_D:
            dirs++;
            break;
        case FTW_F:
            files++;
            break;
        case FTW_SL:
            links++;
            break;
        default:
            others++;
    }

    return 0;
}

int main(int argc, char** argv) {

    for (int i = 1; i < argc; i++) {
        if (nftw(argv[i], fn, MAX_FD,FTW_PHYS) == 0)
            printf("%s - Files: %d, Dirs: %d, Links: %d, Others: %d\n", argv[i], files, dirs, links, others);
        else
            printf("%s: access denied\n", argv[i]);
        files = dirs = links = others = 0;
    }

    return EXIT_SUCCESS;
}