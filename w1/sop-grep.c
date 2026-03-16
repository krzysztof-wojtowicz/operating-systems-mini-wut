#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERR(source) (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), exit(EXIT_FAILURE))

void usage(int argc, char* argv[])
{
    printf("%s pattern\n", argv[0]);
    printf("pattern - string pattern to search at standard input\n");
    exit(EXIT_FAILURE);
}

int main(const int argc, char* argv[])
{
    if (!argv[1])
    {
        usage(argc, &argv[0]);
    }

    int isLine = 0;
    int lineNum = 0;
    char* env = getenv("W1_LINENUMBER");
    if (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "TRUE") == 0))
    {
        isLine = 1;
    }

    const char* pattern = argv[1];

    char* line = NULL;
    size_t line_len = 0;
    while (getline(&line, &line_len, stdin) != -1)  // man 3p getdelim
    {
        if (isLine)
            lineNum++;
        if (strstr(line, pattern))
        {
            if (isLine)
                printf("%d: ", lineNum);
            printf("%s", line);  // getline() should return null terminated data
        }
    }

    if (line)
        free(line);

    return EXIT_SUCCESS;
}
