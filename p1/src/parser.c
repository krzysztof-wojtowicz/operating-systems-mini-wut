#include "parser.h"

// trim spaces from the beginning and from end
void trim_spaces(char* buf)
{
    int start_index = 0;
    int p = 0;

    // find start of command
    while (buf[p] == ' ')
        p++;

    // move characters to the beginning
    while (buf[p] != '\n')
    {
        buf[start_index] = buf[p];
        start_index++;
        p++;
    }

    // remove spaces from end
    while (buf[start_index - 1] == ' ')
        start_index--;

    buf[start_index] = '\0';
}

// command parser returns argc
int parse_command(char* cmd, char** argv)
{
    // tokens count
    int argc = 0;
    // arg length
    int s = 0;
    // pointer
    int start_index = 0;
    // flag for handling "" in paths
    int flag = 0;

    // trim spaces from user input
    trim_spaces(cmd);

    // divide input into tokens
    while (cmd[start_index] != '\0')
    {
        // find end of word
        while (cmd[start_index] != '\0' && (flag == 1 || cmd[start_index] != ' '))
        {
            // if char is '"' => change flag
            if (cmd[start_index] == '"' && flag == 0)
            {
                flag = 1;
            }
            else if (cmd[start_index] == '"' && flag == 1)
            {
                flag = 0;
                s++;
            }
            else
            {
                s++;
            }

            start_index++;
        }
        // allocate memory for arg
        char* arg = malloc(sizeof(char) * (s + 1));
        if (arg == NULL)
            ERR_KILL("malloc");

        // add arg to argv
        argv[argc] = memcpy(arg, cmd + start_index - s, s);
        argv[argc][s] = '\0';
        s = 0;
        argc++;

        if (argc >= MAX_ARGS)
        {
            fprintf(stdout, "Too many arguments!\n");
            return 0;
        }

        // if '"' in arg at the end, delete it
        char* p = strrchr(argv[argc - 1], '"');
        if (p != NULL)
            p[0] = '\0';

        // skip spaces
        while (cmd[start_index] == ' ')
        {
            start_index++;
        }
    }

    return argc;
}

// free argv
void free_argv(char** argv, int const argc)
{
    for (int i = 0; i < argc; i++)
    {
        free(argv[i]);
    }
}
