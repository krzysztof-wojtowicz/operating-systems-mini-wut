#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MSG_SIZE (PIPE_BUF - sizeof(pid_t))
#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s fifo_file file\n", name);
    exit(EXIT_FAILURE);
}

void write_to_fifo(int fifo, int file)
{
    int64_t count;
    char buffer[PIPE_BUF];
    char *buf;

    // write pid to buffer
    *((pid_t *)buffer) = getpid();
    buf = buffer + sizeof(pid_t);

    do
    {
        // read msg from file in a way that it fits in PIPE_BUF with pid
        if ((count = read(file, buf, MSG_SIZE)) < 0)
            ERR("Read:");

        // if there are fewer bytes than MSG_SIZE we have to set the other bytes to 0,
        // so that the buffer has total size of PIPE_BUF
        if (count < MSG_SIZE)
            memset(buf + count, 0, MSG_SIZE - count);

        // if there are bytes read from file, write them to fifo with the PID header
        if (count > 0)
            if (write(fifo, buffer, PIPE_BUF) < 0)
                ERR("Write:");
    } while (count == MSG_SIZE);
}

int main(int argc, char **argv)
{
    int fifo, file;

    // check program arguments
    if (argc != 3)
        usage(argv[0]);

    // create fifo
    if (mkfifo(argv[1], S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) < 0)
        if (errno != EEXIST)
            ERR("create fifo");

    // open fifo for writing
    if ((fifo = open(argv[1], O_WRONLY)) < 0)
        ERR("open");

    // open file for reading
    if ((file = open(argv[2], O_RDONLY)) < 0)
        ERR("file open");

    // handle writing from file to fifo
    write_to_fifo(fifo, file);

    // close file
    if (close(file) < 0)
        perror("Close file:");

    // close fifo
    if (close(fifo) < 0)
        perror("Close fifo:");

    // exit
    return EXIT_SUCCESS;
}