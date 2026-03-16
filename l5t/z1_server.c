#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/limits.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file\n", name);
    exit(EXIT_FAILURE);
}

void read_from_fifo(int fifo) {
    ssize_t count;
    char buffer[PIPE_BUF];

    do
    {
        // read PIPE_BUF bytes from fifo
        if ((count = read(fifo, &buffer, PIPE_BUF)) < 0)
            ERR("read");

        // handle read data
        if (count > 0) {
            // get & print pid
            pid_t pid = *(pid_t *)buffer;
            printf("\nPID:%d------------\n", pid);

            // get & print chars
            for (int i = sizeof(pid_t); i < PIPE_BUF; i++) {
                if (isalpha(buffer[i])) {
                    printf("%c", buffer[i]);
                }
            }
        }

    } while (count > 0);
}

int main (int argc, char** argv) {
    // check program arguments
    if (argc < 2) {
        usage(argv[0]);
    }

    // create fifo file
    int fifo;
    if (mkfifo(argv[1], S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
        if (errno != EEXIST)
            ERR("mkfifo");

    // open fifo
    if ((fifo = open(argv[1], O_RDONLY)) < 0)
        ERR("open");

    // handle reading from fifo
    read_from_fifo(fifo);

    // close fifo
    if (close(fifo) < 0)
        ERR("close");

    // remove fifo
    if (unlink(argv[1]) < 0)
        ERR("unlink");

    return EXIT_SUCCESS;
}