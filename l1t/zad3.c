#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

#define MAX_PATH 200

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *pname)
{
    fprintf(stderr, "USAGE:%s -v <ENVIRONMENT_NAME> [-c] [-i <PACKAGE_NAME>==<VERSION>] [-r <PACKAGE>]\n", pname);
    exit(EXIT_FAILURE);
}

char* new_string(char* dest, char* src) {
    // create new string from src
    dest = malloc(sizeof(char) * (strlen(src) + 1));
    if (dest == NULL) ERR("malloc");

    strcpy(dest, optarg);
    // return ptr to new string
    return dest;
}

void create_env(char* name) {
    // create new dir with 0777 perms
    if (mkdir(name,0777)) ERR("mkdir");

    // create path to requirements
    FILE* req;
    char* path = "./";
    char* req_name = "requirements";
    char* req_path = malloc(sizeof(char) * (strlen(name) + strlen(req_name) + 4));
    // ./<name>/requirements
    strcpy(req_path, path);
    strcat(strcat(strcat(req_path, name), "/"), req_name);

    // create file requirements
    if ((req = fopen(req_path, "w+")) == NULL) ERR("fopen");
    if (fclose(req)) ERR("fclose");

    // free memory
    free(req_path);
}

int check_package(char* package) {
    // check if package installed
    if (fopen(package, "r") != NULL) {
        return -1;
    }

    return 0;
}

void install_package(char* package, char* version) {
    // check if package installed
    if (check_package(package)) {
        fprintf(stderr, "package %s already installed\n", package);
        return;
    }

    FILE* newp;
    // if not installed, create new file with random chars
    // change umask to 0333 - it creates file with 0444 perms (only read)
    mode_t mask = umask(0333);
    if ((newp = fopen(package, "w+")) == NULL) ERR("fopen");
    fprintf(newp, "ggeajhgjkesagjkrsajkgjkafnlkealkg");
    if (fclose(newp)) ERR("fclose");
    umask(mask);

    // add to requirements
    FILE* req;
    if ((req = fopen("requirements", "a")) == NULL) ERR("fopen");
    // <package> <version>
    fprintf(req, "%s %s\n", package, version);

    // close file
    if (fclose(req)) ERR("fclose");
}

void remove_package(char* package) {
    // check if package installed, if not return
    if (check_package(package) == 0) return;

    // remove package file
    if (unlink(package) && errno != ENOENT)
        ERR("unlink");

    // remove from requirements:

    // get requirements file size
    struct stat* stat_buf = malloc(sizeof(struct stat));
    if (stat_buf == NULL) ERR("malloc");

    if (lstat("requirements", stat_buf)) ERR("lstat");
    size_t file_size = stat_buf->st_size;
    free(stat_buf);

    // open requirements for reading
    FILE* req;
    if ((req = fopen("requirements", "r")) == NULL) ERR("fopen");

    // allocate memory for buf
    char *buf = malloc(sizeof(char) * (file_size + 1));
    if (buf == NULL) ERR("malloc");

    // read file to buf
    size_t c = fread(buf, 1, file_size, req);
    if (c != file_size) ERR("fread");

    // close file
    if (fclose(req)) ERR("fclose");

    // add \0 to the end of buf
    buf[file_size] = '\0';

    // find position of package name in buf
    char *p = strstr(buf, package);
    // delete line with package from buf
    size_t len = strlen(package) + 7;
    memmove(p, p + len, strlen(p + len) + 1);

    // overwrite requirements with new buf
    if ((req = fopen("requirements", "w+")) == NULL) ERR("fopen");
    fwrite(buf, 1, file_size-len, req);
    if (fclose(req)) ERR("fclose");

    // free memory
    free(buf);
}

int main(int argc, char** argv) {
    // without arguments print usage
    if (argc < 2) usage(argv[0]);

    // vars init
    int c;
    char* package = NULL;
    char type = '0';
    char** names = NULL;
    int count = 0;

    // get startpath
    char startpath[MAX_PATH];
    if (getcwd(startpath, MAX_PATH) == NULL) ERR("getcwd");

    // get all options
    while ((c = getopt(argc,argv,"v:ci:r:")) != -1) {
        switch (c) {
            case 'v':
                // add all venv names to array
                count++;
                names = realloc(names, sizeof(char*) * count);
                if (names == NULL) ERR("realloc");

                names[count-1] = malloc(sizeof(char) * (strlen(optarg) + 1));
                if (names[count-1] == NULL) ERR("malloc");

                strcpy(names[count-1], optarg);
                break;
            case 'c':
                // change flag to create
                type = 'c';
                break;
            case 'i':
                // get package name and version
                package = new_string(package, optarg);
                // change flag to install
                type = 'i';
                break;
            case 'r':
                // get package name
                package = new_string(package, optarg);
                // change flag to remove
                type = 'r';
                break;
            case '?':
            default:
                usage(argv[0]);
        }
    }

    // without -v option print usage
    if (names == NULL) usage(argv[0]);

    // create new venv only for the first -v
    if (type == 'c') {
        create_env(names[0]);
    }

    // if -i get package name and version from optarg
    char* version;
    if (type == 'i') {
        // find the first '='
        version = strchr(package, '=');
        // if there isn't any '=' print error and usage
        if (version == NULL) {
            fprintf(stderr, "wrong package format <PACKAGE_NAME>==<VERSION>\n");
            usage(argv[0]);
        }
        // replace first '=' with '\0', then char* package returns only name
        version[0] = '\0';

        // jump 2 places in mem -> version returns version
        version += 2;
    }

    // do -i and -r for all venvs
    for (int i = 0; i < count; i++) {
        if (names[i] == NULL) continue;;

        // change dir to venv
        if (chdir(names[i])) {
            fprintf(stderr, "%s: env %s does not exist\n", argv[0], names[i]);
            usage(argv[0]);
        }

        // install
        if (type == 'i') install_package(package, version);

        // remove
        if (type == 'r') remove_package(package);

        // change to startpath
        if (chdir(startpath)) ERR("chdir");

        // free memory
        free(names[i]);
    }

    // free memory
    free(package);
    free(names);

    return EXIT_SUCCESS;
}