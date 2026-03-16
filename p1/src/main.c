#include "command_handler.h"
#include "dict.h"
#include "parser.h"
#include "signal_handler.h"
#include "utils.h"

int main()
{
    // Program usage
    usage();

    // setup:
    char* cmd = NULL;                         // user input line
    size_t len = 0;                           // input length
    char* argv[MAX_ARGS];                     // arguments array
    int argc = 0;                             // arguments count
    Dict* dict = create_dict();               // dict for active copies with workers pids
    FILE* logs = fopen("workers.log", "w+");  // file for storing workers logs

    // signal handling:
    set_ign();                          // ignore all signals
    set_handler(sig_handler, SIGINT);   // handler for SIGINT
    set_handler(sig_handler, SIGTERM);  // handler for SIGTERM
    set_handler(sig_handler, SIGCHLD);  // handler for SIGCHLD

    // Waiting for user input
    while (last_signal != SIGINT && last_signal != SIGTERM)
    {
        // handle children death
        if (last_signal == SIGCHLD)
        {
            last_signal = 0;
            len = 0;
            clearerr(stdin);
            while (1)
            {
                pid_t child_pid = waitpid(0, NULL, WNOHANG);
                if (child_pid == 0)
                {
                    break;
                }

                if (child_pid <= 0)
                {
                    if (errno == ECHILD)
                    {
                        break;
                    }

                    ERR_KILL("waitpid");
                }

                // delete child from workers dict
                fprintf(stdout, "\nWorker [%d] stopped working unexpectedly!\n", child_pid);
                delete_pid(dict, child_pid);
            }
        }

        fprintf(stdout, "> ");
        if (getline(&cmd, &len, stdin) > 1)
        {
            argc = parse_command(cmd, argv);
            // handle user command
            if (argc > 0)
            {
                if (strcmp("add", argv[0]) == 0)
                {
                    handle_add(dict, argv, argc, logs);
                }
                else if (strcmp("end", argv[0]) == 0)
                {
                    handle_end(dict, argv, argc);
                }
                else if (strcmp("list", argv[0]) == 0)
                {
                    handle_list(dict);
                }
                else if (strcmp("restore", argv[0]) == 0)
                {
                    handle_restore(dict, argv, argc, logs);
                }
                else if (strcmp("exit", argv[0]) == 0)
                {
                    break;
                }
                else
                {
                    fprintf(stdout, "Unrecognised command: %s\n", argv[0]);
                }
            }
        }
        // free memory and reset values
        free_argv(argv, argc);
        argc = 0;
        len = 0;
        free(cmd);
        cmd = NULL;
    }

    // exit cleanup
    fprintf(stdout, "\nExiting...\n");
    // kill all workers
    handle_exit(dict);

    // free memory and close files
    if (fclose(logs))
        ERR_KILL("fclose");
    free(cmd);
    free_argv(argv, argc);

    // wait for every child just to make sure
    while (wait(NULL) > 0)
    {
    }

    // exit
    exit(EXIT_SUCCESS);
}
