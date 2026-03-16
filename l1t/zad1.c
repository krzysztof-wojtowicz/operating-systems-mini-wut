#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#define MAX_PATH 200

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(const char *const pname) {
    fprintf(stderr, "USAGE:%s -p dirpath [-o filename]\n", pname);
    exit(EXIT_FAILURE);
}

void list_dir(char* pathname, char* startpath, FILE* output) {
    // change dir
    if (chdir(pathname)) {
        fprintf(stderr,"Directory %s doesnt exist\n", pathname);
        return;
    }

    DIR *dirp;
    // read dir
    if ((dirp = opendir(".")) == NULL) ERR("opendir");

    // get first file and allocate mem for stat struct
    errno = 0;
    struct dirent* file = readdir(dirp);
    struct stat* buf = malloc(sizeof(struct stat));

    fprintf(output,"\nSCIEZKA:\n%s\nLISTA PLIKOW:\n", pathname);

    // read every file from dir
    while (file != NULL) {
        // get stat struct
        if (lstat(file->d_name, buf)) ERR("lstat");

        // print name and size
        fprintf(output,"%s %ld\n", file->d_name, buf->st_size);

        // get next file
        errno = 0;
        file = readdir(dirp);
    }
    // free memory
    free(buf);
    // check for errors
    if (errno != 0) ERR("readdir");
    // close dir
    if (closedir(dirp)) ERR("closedir");
    // change dir to startpath
    if (chdir(startpath)) ERR("chdir");
}

int main (int argc, char** argv) {
    // not enough args -> print usage
    if (argc < 3) usage(argv[0]);

    // vars init
    int c;
    char** pathnames = NULL;
    int size = 0;
    char* filename = NULL;

    // get startpath
    char startpath[MAX_PATH];
    if (getcwd(startpath, MAX_PATH) == NULL) ERR("getcwd");

    // get all options
    while ((c = getopt(argc, argv, "p:o:")) != -1) {
        switch (c) {
            case 'p':
                // add all pathnames to array
                size++;
                pathnames = realloc(pathnames, size*sizeof(char*));
                if (pathnames == NULL) ERR("realloc");

                pathnames[size-1] = malloc(sizeof(optarg));
                if (pathnames[size-1] == NULL) ERR("malloc");
                strcpy(pathnames[size-1], optarg);

                break;
            case 'o':
                // get filename for writing
                // accepted only one time
                if (filename == NULL) filename = optarg;
                else usage(argv[0]);
                break;
            case '?':
            default:
                usage(argv[0]);
        }
    }

    // create output file if -o specified
    FILE *output;
    if (filename == NULL) output = stdout;
    else if ((output = fopen(filename, "w+")) == NULL) ERR("fopen");

    // list dir for every pathname in array
    for (int i = 0; i < size; i++) {
        if (pathnames[i] == NULL) continue;

        list_dir(pathnames[i], startpath, output);
        free(pathnames[i]);
    }
    // free memory
    free(pathnames);
    // close file
    if (fclose(output)) ERR("fclose");

    ///////
    // TODO ETAP 5
    ///////

    return EXIT_SUCCESS;
}