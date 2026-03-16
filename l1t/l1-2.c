#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PATH 101

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void scan_dir() {
    errno = 0;
    DIR* dirp;

    // Open dir
    if ((dirp = opendir(".")) == NULL) ERR("opendir");

    // Initialize variables
    errno = 0;
    struct dirent* element = readdir(dirp);
    struct stat* buf = malloc(sizeof(struct stat));
    int files = 0, dirs = 0, links = 0, others = 0;

    // Read dir in a loop
    while (element != NULL) {
        if (lstat(element->d_name, buf)) ERR("lstat");

        if (S_ISREG(buf->st_mode)) files++;
        else if (S_ISDIR(buf->st_mode)) dirs++;
        else if (S_ISLNK(buf->st_mode)) links++;
        else others++;

        errno = 0;
        element = readdir(dirp);
    }

    // Free memory & close dir
    free(buf);

    // Check for errors in readdir
    if (errno != 0) ERR("readir");

    // Close dir
    errno = 0;
    if (closedir(dirp)) ERR("closedir");

    // Print results
    printf("\tFiles: %d, Dirs: %d, Links: %d, Others: %d\n", files, dirs, links, others);
}

int main(int argc, char **argv) {
    char pathName[MAX_PATH];

    if ((getcwd(pathName, MAX_PATH)) == NULL) ERR("getcwd");

    for (int i = 1; i < argc; i++) {
        if (chdir(argv[i])) {
            printf("Directory %s doesnt exist\n", argv[i]);
            continue;
        }

        printf("Directory: %s\n", argv[i]);
        scan_dir();

        if (chdir(pathName)) ERR("chdir");
    }

    return EXIT_SUCCESS;
}
