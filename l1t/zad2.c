#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <ftw.h>

#define MAX_PATH 200

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *pname)
{
    fprintf(stderr, "USAGE:%s -p dirname [-d depth] [-e extension] [-o]\n", pname);
    exit(EXIT_FAILURE);
}

// OLD
// callback fn for nftw
// int list(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
//     // if file print name and size
//     if (typeflag == FTW_F) {
//         fprintf(stdout, "%s %ld\n", fpath+ftwbuf->base, sb->st_size);
//     }
//     // if dir print path
//     else if (typeflag == FTW_D) {
//         fprintf(stdout, "path: %s\n", fpath);
//     }
//     return 0;
// }

char* get_last(char* str, char c) {
    // get string after last character c
    // example: for c='.' it returns file extension (file.txt) -> txt
    char* last = strrchr(str, c);
    if (last != NULL)
        last++;
    else
        last = str;

    return last;
}

void list_rec(char* pathname, char* startpath, int max_fd, char* ext, FILE* output) {
    // if max depth, end recursion
    if (max_fd == 0) return;

    // change dir for pathname
    if (chdir(pathname)) {
        fprintf(stderr, "%s doesnt exist\n", pathname);
        return;
    }

    // get the actual path and print it
    char actualpath[MAX_PATH];
    if ((getcwd(actualpath, MAX_PATH)) == NULL) ERR("getcwd");
    fprintf(output, "path: %s\n", actualpath);

    // opendir for listing
    DIR* dirp;
    if ((dirp = opendir(".")) == NULL) ERR("opendir");

    // read dir and allocate mem for struct stat
    errno = 0;
    struct dirent* file = readdir(dirp);
    struct stat* buf = malloc(sizeof(struct stat));
    // vars init
    char** dirs = NULL;
    int count = 0;
    char* file_ext = NULL;

    // read all files
    while (file != NULL) {
        // get stat struct
        if (lstat(file->d_name, buf)) ERR("lstat");

        // for file, check extension and print: <name> <size>
        if (S_ISREG(buf->st_mode)) {
            if (ext == NULL) fprintf(output, "%s %ld\n", file->d_name, buf->st_size);
            else {
                file_ext = get_last(file->d_name, '.');

                if (strcmp(file_ext, ext) == 0)
                    fprintf(output, "%s %ld\n", file->d_name, buf->st_size);
            }
        }
        // for dir, append it to the subdirectories array for the next recursion step
        else if (S_ISDIR(buf->st_mode)) {
            // skip . and .. directories
            if (strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0) {
                count++;
                dirs = realloc(dirs, count*sizeof(char*));
                if (dirs == NULL) ERR("realloc");

                dirs[count-1] = malloc(sizeof(char) * (strlen(file->d_name)+1));
                if (dirs[count-1] == NULL) ERR("malloc");

                strcpy(dirs[count-1], file->d_name);
            }
        }

        // get next file in dir
        errno = 0;
        file = readdir(dirp);
    }
    // free memory
    free(buf);
    // check for errors
    if (errno != 0) ERR("readdir");
    // close fir
    if (closedir(dirp)) ERR("closedir");
    // if there aren't any subdirectories -> end recursion
    if (dirs == NULL) return;

    // next recursion step for every subdirectory
    for (int i = 0; i < count; i++) {
        if (dirs[i] == NULL) continue;

        // recursion with depth-1
        list_rec(dirs[i], actualpath, max_fd-1, ext, output);

        // free memory
        free(dirs[i]);
        // change dir back to the directory where recursion started
        if (chdir(actualpath)) ERR("chdir");
    }

    // free memory
    free(dirs);
}

int main (int argc, char** argv) {
    // without args print usage
    if (argc < 2) usage(argv[0]);

    // set env for output file
    if (putenv("L1_OUTPUTFILE=output.txt") != 0) ERR("putenv");

    // vars init
    int c;
    int max_fd = 1;
    char* ext = NULL;
    FILE* output = stdout;
    char **pathnames = NULL;
    int count = 0;
    char* env = NULL;

    // get startpath
    char startpath[MAX_PATH];
    if (getcwd(startpath, MAX_PATH) == NULL) ERR("getcwd");

    // get all options
    while ((c = getopt(argc, argv, "p:d:e:o")) != -1)
        switch (c) {
            case 'p':
                // add all paths to array
                count++;
                pathnames = realloc(pathnames, count*sizeof(char*));
                if (pathnames == NULL) ERR("realloc");

                pathnames[count-1] = malloc(sizeof(char) * (strlen(optarg) + 1));
                if (pathnames[count-1] == NULL) ERR("malloc");

                strcpy(pathnames[count-1], optarg);
                break;
            case 'd':
                // set max dept if specified, default is 1
                max_fd = (int)strtol(optarg, (char **)NULL, 10);
                break;
            case 'e':
                // set file extension, default prints all files
                if (ext == NULL) {
                    ext = malloc(sizeof(char) * (strlen(optarg) + 1));
                    strcpy(ext, optarg);
                }
                break;
            case 'o':
                // if -o get filename from env
                env = getenv("L1_OUTPUTFILE");
                break;
            case '?':
            default:
                usage(argv[0]);
        }

    // filename exists, create new file (or overwrite existing)
    if (env != NULL) {
        if ((output = fopen(env, "w+")) == NULL) ERR("fopen");
    }

    // list dir recursively for every path
    for (int i = 0; i < count; i++) {
        if (pathnames[i] == NULL) continue;

        // start recursion
        list_rec(pathnames[i], startpath, max_fd, ext, output);

        // change pathname back to start
        if (chdir(startpath)) ERR("chdir");
        // free memory
        free(pathnames[i]);
    }
    // free memory
    free(pathnames);
    free(ext);

    // close file
    if (env != NULL) {
        if (fclose(output)) ERR("fclose");
    }

    return EXIT_SUCCESS;
}