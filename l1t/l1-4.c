#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *pname)
{
    fprintf(stderr, "USAGE:%s -n Name -p OCTAL(0xxx) -s SIZE\n", pname);
    exit(EXIT_FAILURE);
}

void new_file(char* name, mode_t octal, size_t size) {
    // Random generator
    srand(time(NULL));
    // Change umask
    mode_t mask = umask(~octal & 0777);
    // New file
    FILE* newF;
    if ((newF = fopen(name, "w+")) == NULL) ERR("fopen");
    // Return to the previous value of umask
    umask(mask);
    // Generate data for the file
    char* data;
    if ((data = malloc(sizeof(char)*size)) == NULL) ERR("malloc");

    for (int i = 0; i < size; i++) {
        int j = rand() % 10 + 1;

        if (j == 1) data[i] = rand() % ('Z' - 'A' + 1) + 'A';
        else data[i] = 0;
    }

    // Write data to the file
    size_t n = fwrite(data, 1, size, newF);
    free(data);
    if (n != size) ERR("fwrite");

    // Close file
    if (fclose(newF)) ERR("fclose");
}


int main(int argc, char** argv) {
    if (argc < 7) usage(argv[0]);

    int c;
    char* name = NULL;
    mode_t octal = -1;
    size_t size = -1;

    // Read arguments
    while ((c = getopt(argc, argv, "p:n:s:")) != -1)
        switch (c)
        {
        case 'p':
                octal = strtol(optarg, (char **)NULL, 8);
                break;
        case 's':
                size = strtol(optarg, (char **)NULL, 10);
                break;
        case 'n':
                name = optarg;
                break;
        case '?':
        default:
                usage(argv[0]);
        }

    if (name == NULL || octal == (mode_t)-1 || size == -1) usage(argv[0]);

    // If exists -> remove
    if (unlink(name) && errno != ENOENT) ERR("unlink");

    // Create file
    new_file(name, octal, size);

    return EXIT_SUCCESS;
}
