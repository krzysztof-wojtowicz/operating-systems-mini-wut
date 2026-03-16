#include "utils.h"

// program usage
void usage()
{
    fprintf(stdout, "-------------------------- Backup wizard --------------------------\n");
    fprintf(stdout, "Commands:\n");
    fprintf(stdout, "    - add <source path> <target path>\n");
    fprintf(stdout, "       > starts backup of folder <source path> to <target path>\n");
    fprintf(stdout, "    - end <source path> <target path>\n");
    fprintf(stdout, "       > ends backup\n");
    fprintf(stdout, "    - list\n");
    fprintf(stdout, "       > lists all folders that have backups\n");
    fprintf(stdout, "    - restore <source path> <target path>\n");
    fprintf(stdout, "       > restores backup\n");
    fprintf(stdout, "    - exit\n");
    fprintf(stdout, "       > ends program\n");
    fprintf(stdout, "--------------------------------------------------------------------\n");
}

// check dir
int check_dir(char* path)
{
    errno = 0;
    DIR* dir = opendir(path);

    // permission denied
    if (dir == NULL && errno == EACCES)
    {
        fprintf(stdout, "Access to %s denied, cannot start backup!\n", path);
        return -1;
    }
    // dir doesn't exits
    if (dir == NULL && errno == ENOENT)
    {
        // create new dir for backup
        if (mkdir(path, 0777) != 0)
        {
            ERR("mkdir, worker didn't start");
            return -1;
        }

        return 0;
    }
    // path is not a directory
    if (dir == NULL && errno == ENOTDIR)
    {
        fprintf(stdout, "%s is not a directory.\n", path);
        return -1;
    }

    // directory exists -> we have to check if it's empty
    struct dirent* file_info;

    while ((file_info = readdir(dir)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }

        fprintf(stdout, "Directory %s is not empty.\n", path);
        if (closedir(dir) < 0)
        {
            ERR("closedir, worker didn't start");
            return -1;
        }
        return -1;
    }

    closedir(dir);
    return 0;
}

// join path with filename and return new ptr
char* join_paths(char* path, char* filename)
{
    int size = strlen(path) + strlen(filename) + 2;
    char* full_path = malloc(sizeof(char) * size);
    if (full_path == NULL)
    {
        ERR("malloc");
        exit(EXIT_FAILURE);
    }

    strcpy(full_path, path);
    strcat(full_path, "/");
    strcat(full_path, filename);

    return full_path;
}

void rm_dir_recursive(char* path)
{
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        ERR("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* file_info;
    struct stat stat_info;

    // read all dir files
    while ((file_info = readdir(dir)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }

        char* file = join_paths(path, file_info->d_name);

        if (lstat(file, &stat_info) != 0)
        {
            ERR("lstat");
            exit(EXIT_FAILURE);
        }

        if (S_ISREG(stat_info.st_mode) || S_ISLNK(stat_info.st_mode))
        {
            if (unlink(file) < 0)
            {
                ERR("unlink");
                exit(EXIT_FAILURE);
            }
        }
        else if (S_ISDIR(stat_info.st_mode))
        {
            rm_dir_recursive(file);
        }
        free(file);
    }

    if (closedir(dir) < 0)
    {
        ERR("closedir");
        exit(EXIT_FAILURE);
    }

    if (rmdir(path) < 0)
    {
        ERR("rmdir");
        exit(EXIT_FAILURE);
    }
}
