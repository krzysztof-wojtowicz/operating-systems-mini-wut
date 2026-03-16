#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

// join 2 path. returned pointer is for newly allocated memory and must be freed
char* join_paths(const char* path1, const char* path2)
{
    char* res;
    const int l1 = strlen(path1);
    if (path1[l1 - 1] == '/')
    {
        res = malloc(strlen(path1) + strlen(path2) + 1);
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
    }
    else
    {
        res = malloc(strlen(path1) + strlen(path2) + 2);  // additional space for "/"
        if (!res)
            ERR("malloc");
        strcpy(res, path1);
        res[l1] = '/';
        res[l1 + 1] = 0;
    }
    return strcat(res, path2);
}

void usage(int argc, char** argv)
{
    (void)argc;
    fprintf(stderr, "USAGE: %s [path to base file]\n", argv[0]);
    exit(EXIT_FAILURE);
}

char** get_book_data(FILE* book)
{
    ssize_t c;
    char* line = NULL;
    size_t n = 0;
    char* value = NULL;
    int index = -1;
    // array for storing book data
    char** data = malloc(3 * sizeof(char*));
    if (data == NULL)
        ERR("malloc");

    data[0] = NULL;
    data[1] = NULL;
    data[2] = NULL;

    while ((c = getline(&line, &n, book)) != -1)
    {
        // divide line: <line:value>
        value = strchr(line, ':');
        if (value != NULL)
        {
            value[0] = '\0';
            value++;

            // check if desired field
            if (strcmp(line, "author") == 0)
                index = 0;
            else if (strcmp(line, "title") == 0)
                index = 1;
            else if (strcmp(line, "genre") == 0)
                index = 2;

            // save value
            if (index != -1)
            {
                data[index] = malloc(sizeof(char) * (strlen(value) + 1));
                if (data[index] == NULL)
                    ERR("malloc");

                strcpy(data[index], value);
                index = -1;
            }
        }
    }

    // check for error
    // if (errno != 0)
    //     ERR("getline");

    // free memory
    if (line != NULL)
        free(line);

    return data;
}

void print_book_data(char** data)
{
    if (data[0] != NULL)
    {
        fprintf(stdout, "author: %s", data[0]);
    }
    else
        fprintf(stdout, "author: missing!\n");

    if (data[1] != NULL)
    {
        fprintf(stdout, "title: %s", data[1]);
    }
    else
        fprintf(stdout, "title: missing!\n");

    if (data[2] != NULL)
    {
        fprintf(stdout, "genre: %s", data[2]);
    }
    else
        fprintf(stdout, "genre: missing!\n");
}

void free_data(char** data)
{
    for (int i = 0; i < 3; i++)
        if (data[i] != NULL)
            free(data[i]);

    free(data);
}

void create_symlink(char* dir, char* name, char* file_path)
{
    char* sym_path = join_paths(dir, name);
    if (symlink(file_path, sym_path))
        ERR("symlink");
    free(sym_path);
}

int walk(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    FILE* book;
    char* filename;
    char* temp_path;
    char* temp2_path = NULL;
    char* gen_path = NULL;
    char** data;

    if (typeflag == FTW_F)
    {
        // get filename
        filename = strrchr(fpath, '/');
        filename++;
        // create path to the original file
        temp_path = join_paths("..", fpath);

        // create symlink in by-visible-title
        create_symlink("by-visible-title", filename, temp_path);

        // get book data
        if ((book = fopen(fpath, "r")) == NULL)
            ERR("fopen");
        data = get_book_data(book);
        if (fclose(book))
            ERR("fclose");

        // create symlink in by-title if title exist
        if (data[1] != NULL)
        {
            if (strlen(data[1]) > 64)
                data[1][64] = '\0';
            create_symlink("by-title", data[1], temp_path);
        }

        // create symlink in by-genre if genre and title exist in metadata
        if (data[2] != NULL && data[1] != NULL)
        {
            if (strlen(data[2]) > 64)
                data[2][64] = '\0';
            gen_path = join_paths("by-genre", data[2]);
            mkdir(gen_path, 0777);
            temp2_path = join_paths("..", temp_path);
            create_symlink(gen_path, data[1], temp2_path);
            free(temp2_path);
        }

        // free memory
        free_data(data);
        free(temp_path);
        if (gen_path != NULL)
            free(gen_path);
    }

    return 0;
}

int main(int argc, char** argv)
{
    // program doesn't accept more than 1 argument
    if (argc > 2)
        usage(argc, argv);

    //---------ETAP 1---------------------
    // open book file
    // FILE* book;
    // if ((book = fopen(argv[1], "r")) == NULL)
    //     ERR("fopen");
    //
    // char** data = get_book_data(book);
    // print_book_data(data);
    // free_data(data);
    //
    // if (fclose(book))
    //     ERR("fclose");

    //--------ETAP 2 & 3---------------------
    // create directories
    if (argc == 1)
    {
        if (mkdir("index", 0777))
            ERR("mkdir");
        if (mkdir("./index/by-visible-title", 0777))
            ERR("mkdir");
        if (mkdir("./index/by-title", 0777))
            ERR("mkdir");
        if (mkdir("./index/by-genre", 0777))
            ERR("mkdir");
        // search library
        if (chdir("./index"))
            ERR("chdir");
        if (nftw("../library", walk, 5, FTW_PHYS))
            ERR("nftw");
        if (chdir(".."))
            ERR("chdir");
    }

    //--------ETAP 4-----------
    if (argc == 2)
    {
        FILE* base;

        // open base file
        if ((base = fopen(argv[1], "r")) == NULL)
            ERR("fopen");

        // close base file
        if (fclose(base))
            ERR("fclose");
    }

    return EXIT_SUCCESS;
}
