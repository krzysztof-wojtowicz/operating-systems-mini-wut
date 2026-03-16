#include "command_handler.h"

// handle add command
void handle_add(Dict* dict, char** argv, int argc, FILE* logs)
{
    if (argc < 3)
    {
        fprintf(stdout, "Not enough arguments for add\n");
        return;
    }

    char* src = argv[1];

    if (check_path(src) < 0)
    {
        fprintf(stdout, "Invalid source path: %s\n", src);
        return;
    }

    for (int i = 2; i < argc; i++)
    {
        char* target = argv[i];
        // check if backup for given src&target pair already exists
        if (search(dict, src, target) != 0)
        {
            fprintf(stdout, "Backup for %s to %s already exists!\n", src, target);
            continue;
        }
        // create child worker:
        pid_t pid = fork();

        switch (pid)
        {
            case 0:
                set_handler(SIG_IGN, SIGINT);  // only parent handles SIGINT

                // check if target directory exists/is empty
                if (check_dir(target) != 0)
                    exit(EXIT_FAILURE);

                // real path of source directory and target directory
                char* src_path = realpath(src, NULL);
                if (src_path == NULL)
                {
                    ERR("realpath, worker didn't start");
                    exit(EXIT_FAILURE);
                }
                char* target_path = realpath(target, NULL);
                if (target_path == NULL)
                {
                    ERR("realpath, worker didn't start");
                    exit(EXIT_FAILURE);
                }

                start_worker(src_path, target_path, logs);
                exit(EXIT_FAILURE);
            case -1:
                ERR("fork, worker didn't start");
                return;
            default:
                break;
        }

        // add to dictionary
        insert(dict, src, target, pid);
        fprintf(stdout, "Started backup for %s to %s, with worker %d.\n", src, target, pid);
    }
}

// handle end command,
void handle_end(Dict* dict, char** argv, int argc)
{
    if (argc < 3)
    {
        fprintf(stdout, "Not enough arguments for end\n");
        return;
    }

    char* src = argv[1];

    if (check_path(src) < 0)
    {
        fprintf(stdout, "Invalid source path: %s\n", src);
        return;
    }

    for (int i = 2; i < argc; i++)
    {
        char* target = argv[i];

        if (check_path(target) < 0)
        {
            fprintf(stdout, "Invalid target path: %s\n", target);
            return;
        }

        // find pid of worker and delete it from dictionary
        pid_t pid = delete_key(dict, src, target);

        // if backup for src&target doesn't exist continue
        if (pid == 0)
        {
            fprintf(stdout, "There isn't an active backup for %s -> %s\n", src, target);
            continue;
        }

        // kill worker process
        if (kill(pid, SIGTERM) < 0)
        {
            ERR_KILL("kill");
        }

        if (waitpid(pid, NULL, 0) < 0)
        {
            ERR_KILL("waitpid");
        }

        fprintf(stdout, "Ended backup for %s to %s, killed worker %d.\n", src, target, pid);
    }
}

// handle list command
void handle_list(Dict* dict)
{
    if (dict->size == 0)
    {
        fprintf(stdout, "No active copies.\n");
        return;
    }

    fprintf(stdout, "Active copies:\n");
    print_dict(dict);
}

// handle restore
void handle_restore(Dict* dict, char** argv, int argc, FILE* logs)
{
    if (argc < 3)
    {
        fprintf(stdout, "Not enough arguments for restore\n");
        return;
    }

    char* src = argv[1];
    char* target = argv[2];

    // check if paths are correct
    if (check_path(src) < 0)
    {
        fprintf(stdout, "Invalid source path: %s\n", src);
        return;
    }
    if (check_path(target) < 0)
    {
        fprintf(stdout, "Invalid target path: %s\n", target);
        return;
    }

    // check if there is a running copy
    if (search(dict, src, target) != 0)
    {
        fprintf(stdout, "Backup for %s to %s is active!\n", src, target);
        handle_list(dict);
        return;
    }

    // create restorer child
    pid_t pid = fork();

    switch (pid)
    {
        case 0:
            // restore
            fprintf(stdout, "Restoring %s to %s.", target, src);
            write_log(logs, src, target, "New restorer", "");
            // restore(dict, src, target, logs);
            restore_better(dict, src, target, logs);
            break;
        case -1:
            ERR("fork, restore didn't happen");
            return;
        default:
            break;
    }

    // block and wait for restorer
    pid_t wait_pid = waitpid(pid, NULL, 0);
    while (wait_pid < 0 && errno == EINTR)
    {
        wait_pid = waitpid(pid, NULL, 0);
    }

    if (wait_pid < 0 && errno != ECHILD)
    {
        ERR("waitpid");
    }
}

// handle restore command, deletes src and copies target
void restore(Dict* dict, char* src, char* target, FILE* logs)
{
    // real path of source directory and target directory
    char* src_path = realpath(src, NULL);
    if (src_path == NULL)
    {
        ERR("realpath, restore failed");
        exit(EXIT_FAILURE);
    }
    char* target_path = realpath(target, NULL);
    if (target_path == NULL)
    {
        ERR("realpath, restore failed");
        exit(EXIT_FAILURE);
    }
    // rm src and then copy target
    rm_dir_recursive(src);
    fprintf(stdout, ".");
    if (mkdir(src, 0777))
    {
        ERR("mkdir");
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, ".");
    copy_dir(target_path, src_path, target_path, src_path, logs);
    fprintf(stdout, ".\n");

    // free memory
    free(src_path);
    free(target_path);
    exit(EXIT_SUCCESS);
}

// handle restore command more effectively, only copy files that did change
void restore_better(Dict* dict, char* src, char* target, FILE* logs)
{
    // real path of source directory and target directory
    char* src_path = realpath(src, NULL);
    if (src_path == NULL)
    {
        ERR("realpath, restore failed");
        exit(EXIT_FAILURE);
    }
    char* target_path = realpath(target, NULL);
    if (target_path == NULL)
    {
        ERR("realpath, restore failed");
        exit(EXIT_FAILURE);
    }

    // restore
    // delete all files from source that don't exist in target
    fprintf(stdout, ".");
    delete_recursive(src_path, target_path, logs);

    // restore file that needs update from target
    fprintf(stdout, ".");
    restore_recursive(src_path, target_path, logs);

    // free memory
    fprintf(stdout, ".\n");
    free(src_path);
    free(target_path);
    exit(EXIT_SUCCESS);
}

// check if file in the source needs updating, 1 if needs update, 0 if doesn't
// src_path and target_path are paths to given files
int needs_update(char* src_path, char* target_path)
{
    struct stat src_stat, target_stat;

    // check if file exists in source
    if (lstat(src_path, &src_stat) == -1)
    {
        return 1;  // doesn't exist or lstat error, so copy it from target
    }

    // get target file info
    if (lstat(target_path, &target_stat) == -1)
    {
        return 0;  // error in lstat, omit copying
    }

    // check mtime and size, if they differ -> file needs updating
    if (src_stat.st_size != target_stat.st_size || src_stat.st_mtim.tv_sec != target_stat.st_mtim.tv_sec ||
        src_stat.st_mtim.tv_nsec != target_stat.st_mtim.tv_nsec)
    {
        return 1;  // different mtime or size -> needs change
    }

    // src file is the same as target file
    return 0;
}

// recursively delete files from src that don't exist in target
void delete_recursive(char* src, char* target, FILE* logs)
{
    DIR* src_dir = opendir(src);
    if (src_dir == NULL)
    {
        ERR("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* file_info;
    struct stat stat_info;

    // read all dir files
    while ((file_info = readdir(src_dir)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }

        char* file_src = join_paths(src, file_info->d_name);
        char* file_target = join_paths(target, file_info->d_name);

        // get src file info
        if (lstat(file_src, &stat_info) != 0)
        {
            ERR("lstat");
            exit(EXIT_FAILURE);
        }

        // check if target file exists
        if (lstat(file_target, &stat_info) != 0)
        {
            if (errno != ENOENT)
            {
                ERR("lstat");
                exit(EXIT_FAILURE);
            }

            // else it doesn't exist - delete it from src
            if (S_ISREG(stat_info.st_mode) || S_ISLNK(stat_info.st_mode))
            {
                if (unlink(file_src) < 0)
                {
                    ERR("unlink");
                    exit(EXIT_FAILURE);
                }
                write_log(logs, src, target, "Delete file ", file_src);
            }
            else if (S_ISDIR(stat_info.st_mode))
            {
                rm_dir_recursive(file_src);
                write_log(logs, src, target, "Delete directory ", file_src);
            }
        }
        // check dir
        else if (S_ISDIR(stat_info.st_mode))
        {
            delete_recursive(file_src, file_target, logs);
        }

        free(file_src);
        free(file_target);
    }

    if (closedir(src_dir) < 0)
    {
        ERR("closedir");
        exit(EXIT_FAILURE);
    }
}

// restore recursively files that needs update from target to src
void restore_recursive(char* src, char* target, FILE* logs)
{
    DIR* target_dir = opendir(target);
    if (target_dir == NULL)
    {
        ERR("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* file_info;
    struct stat stat_info;

    // read all dir files
    while ((file_info = readdir(target_dir)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }

        char* file_src = join_paths(src, file_info->d_name);
        char* file_target = join_paths(target, file_info->d_name);

        // get target file info
        if (lstat(file_target, &stat_info) != 0)
        {
            ERR("lstat");
            exit(EXIT_FAILURE);
        }

        // check if file needs changing
        if (needs_update(file_src, file_target) == 1)
        {
            // copy regular file
            if (S_ISREG(stat_info.st_mode))
            {
                copy_file(file_target, file_src);
                write_log(logs, src, target, "Restore file ", file_src);
            }
            // copy symlink
            else if (S_ISLNK(stat_info.st_mode))
            {
                copy_symlink(file_target, file_src, target, src, logs);
                write_log(logs, src, target, "Restore symlink ", file_src);
            }
        }
        // recursive for dir
        if (S_ISDIR(stat_info.st_mode))
        {
            // make sure dir exists in src
            if (mkdir(file_src, 0777) < 0 && errno != EEXIST)
            {
                ERR("mkdir");
                exit(EXIT_FAILURE);
            }
            copy_permissions(file_target, file_src);  // set correct permissions

            // run restore recursively
            write_log(logs, src, target, "Restore directory ", file_src);
            restore_recursive(file_src, file_target, logs);
        }

        free(file_src);
        free(file_target);
    }

    if (closedir(target_dir) < 0)
    {
        ERR("closedir");
        exit(EXIT_FAILURE);
    }
}

// handle exit command
void handle_exit(Dict* dict)
{
    // kill all workers
    Node* p = dict->head;
    while (p != NULL)
    {
        if (kill(p->pid, SIGTERM))  // send SIGTERM to worker with pid
        {
            ERR_KILL("kill");
        }
        // wait for worker child to finish
        pid_t wait_pid = waitpid(p->pid, NULL, 0);
        while (wait_pid < 0 && errno == EINTR)
        {
            wait_pid = waitpid(p->pid, NULL, 0);
        }

        if (wait_pid < 0 && errno != ECHILD)
        {
            ERR_KILL("waitpid");
        }
        p = p->next;  // next
    }
    // free memory
    free_dict(dict);
}

// check if path is correct for directory
int check_path(char* path)
{
    struct stat stat_info;

    if (stat(path, &stat_info) == 0 && S_ISDIR(stat_info.st_mode))
    {
        return 0;
    }

    return -1;
}
