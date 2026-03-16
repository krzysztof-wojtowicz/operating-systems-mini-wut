#include "worker.h"

// start worker
void start_worker(char* src, char* target, FILE* logs)
{
    // start new worker
    write_log(logs, src, target, "New worker", "");
    write_log(logs, src, target, "Copying source dir: ", src);

    // check if target isn't inside source
    if (path_cmp(src, target) == 0)
    {
        fprintf(stdout, "\nInvalid target, can't be inside source\n");
        exit(EXIT_FAILURE);
    }

    // initial copy
    copy_dir(src, target, src, target, logs);

    // inotify init
    Watchers* watchers = watchers_init();
    add_watch_recursive(watchers, src);
    print_watchers(watchers, logs, src, target);

    // wait for changes and handle them
    write_log(logs, src, target, "Waiting for changes...", "");
    while (last_signal != SIGTERM && watchers->size > 0)
    {
        // handle inotify events
        read_watch(watchers, src, target, logs);
    }

    // exit cleanup
    write_log(logs, src, target, "Worker exiting...", "");
    // free inotify and watchers
    free_watchers(watchers);
    free(target);

    // exit
    exit(EXIT_SUCCESS);
}

// write log to workers.log file
void write_log(FILE* logs, char* src, char* target, char* msg, char* arg)
{
    fprintf(logs, "[%d] %s '%s'\n", getpid(), msg, arg);
    fflush(logs);
}

// copy file from file1 to file2
void copy_file(char* file1, char* file2)
{
    FILE* src = fopen(file1, "r");
    if (src == NULL)
    {
        ERR("fopen");
        exit(EXIT_FAILURE);
    }

    FILE* dst = fopen(file2, "w");
    if (dst == NULL)
    {
        ERR("fopen");
        exit(EXIT_FAILURE);
    }

    char buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
    {
        fwrite(buf, 1, n, dst);
    }

    // copy permissions
    copy_permissions(file1, file2);

    // close files
    if (fclose(src))
    {
        ERR("fclose");
        exit(EXIT_FAILURE);
    }
    if (fclose(dst))
    {
        ERR("fclose");
        exit(EXIT_FAILURE);
    }
}

// copy symlink
void copy_symlink(char* file1, char* file2, char* src, char* target, FILE* logs)
{
    char* link_path = realpath(file1, NULL);

    if (link_path == NULL && errno == ENOENT)
    {
        return;
    }
    if (link_path == NULL)
    {
        ERR("realpath");
        exit(EXIT_FAILURE);
    }

    // compare real paths
    if (path_cmp(src, link_path) == 0)
    {
        write_log(logs, src, target, "Link inside src:\n\t", link_path);
        // create symlink to file in target dir
        // get ending of path after src real path
        char* new_path = link_path + strlen(src) + 1;
        new_path = join_paths(target, new_path);

        if (symlink(new_path, file2) != 0)
        {
            ERR("symlink");
            exit(EXIT_FAILURE);
        }

        free(new_path);
    }
    else
    {
        write_log(logs, src, target, "Link outside src:\n\t", link_path);
        // create symlink without changes to path
        if (symlink(link_path, file2) != 0)
        {
            ERR("symlink");
            exit(EXIT_FAILURE);
        }
    }

    free(link_path);
}

// copy whole directory from path1 to path2
void copy_dir(char* path1, char* path2, char* src_path, char* target_path, FILE* logs)
{
    // open src dir
    DIR* src = opendir(path1);
    if (src == NULL)
    {
        ERR("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent* file_info;
    struct stat stat_info;

    // read all dir files
    while ((file_info = readdir(src)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }

        char* file1 = join_paths(path1, file_info->d_name);
        char* file2 = join_paths(path2, file_info->d_name);

        if (lstat(file1, &stat_info) != 0)
        {
            ERR("lstat");
            exit(EXIT_FAILURE);
        }

        // copy regular file
        if (S_ISREG(stat_info.st_mode))
        {
            write_log(logs, path1, path2, "Copying file: ", file_info->d_name);
            copy_file(file1, file2);
        }
        // copy whole directory recursively
        else if (S_ISDIR(stat_info.st_mode))
        {
            write_log(logs, path1, path2, "Copying dir: ", file_info->d_name);
            if (mkdir(file2, 0777) != 0)
            {
                ERR("mkdir");
                exit(EXIT_FAILURE);
            }
            // set permissions
            copy_permissions(file1, file2);
            // copy directory recursively
            copy_dir(file1, file2, src_path, target_path, logs);
        }
        // copy link
        else if (S_ISLNK(stat_info.st_mode))
        {
            write_log(logs, path1, path2, "Copying link: ", file_info->d_name);
            // copy symlink
            copy_symlink(file1, file2, src_path, target_path, logs);
        }

        free(file1);
        free(file2);
    }

    if (closedir(src) < 0)
    {
        ERR("closedir");
        exit(EXIT_FAILURE);
    }
}

// compare if path2 includes path1
int path_cmp(char* path1, char* path2)
{
    int len = strlen(path1);
    int len2 = strlen(path2);

    // path2 shorter than path1 -> can't include path1
    if (len2 < len)
        return -1;

    // compare
    for (int i = 0; i < len; i++)
    {
        if (path1[i] != path2[i])
            return -1;
    }

    if (path2[len] != '\0' && path2[len] != '/')
    {
        return -1;
    }

    // if path2 includes path1 return 0
    return 0;
}

// read inotify fd and handle events
void read_watch(Watchers* w, char* src, char* target, FILE* logs)
{
    // cookie for moved from & moved to event
    uint32_t pending_cookie = 0;
    char pending_move_path[PATH_MAX] = "";

    char buffer[EVENT_BUF_LEN];
    // read buffer from inotify
    ssize_t len = read(w->fd, buffer, EVENT_BUF_LEN);
    if (len < 0 && errno == EINTR)
    {
        // interrupted by signal - exit
        return;
    }
    if (len < 0)
    {
        ERR("read");
        exit(EXIT_FAILURE);
    }

    // handle buffer
    ssize_t i = 0;
    while (i < len)
    {
        // get event struct
        struct inotify_event* event = (struct inotify_event*)&buffer[i];

        // log
        fprintf(logs, "[%d] New event: [i=%ld] [ev=0x%08x] [wd=%d]\n", getpid(), i, event->mask, event->wd);

        // find watch path
        Watch* watch = search_watch(w, event->wd);

        // create event path
        char* event_path = NULL;
        if (watch && event->len > 0)
        {
            event_path = join_paths(watch->path, event->name);
        }
        else if (watch)
        {
            event_path = malloc(sizeof(char) * (strlen(watch->path) + 1));
            if (event_path == NULL)
            {
                ERR("malloc");
                exit(EXIT_FAILURE);
            }
            strcpy(event_path, watch->path);
        }

        // handle event
        if (event->mask & IN_IGNORED)
        {
            // watch was removed by the kernel
            fprintf(logs, "\t Removed watch [wd=%d]\n", event->wd);
            fflush(logs);
            delete_watch(w, event->wd);
        }
        // handle directories
        else if (event->mask & IN_ISDIR)
        {
            fprintf(logs, "\tDirectory '%s' was ", event_path);

            if (event->mask & IN_CREATE)
            {
                fprintf(logs, "CREATED\n");
                // make new dir in the backup directory
                char* file_path = src2target_path(event_path, src, target);
                if (mkdir(file_path, 0777) < 0)
                {
                    ERR("mkdir");
                    exit(EXIT_FAILURE);
                }
                // copy permissions
                copy_permissions(event_path, file_path);
                // copy dir files and subdirs into backup
                copy_dir(event_path, file_path, src, target, logs);
                free(file_path);

                // add new watches
                add_watch_recursive(w, strdup(event_path));
                print_watchers(w, logs, src, target);
            }
            if (event->mask & IN_DELETE)
            {
                fprintf(logs, "DELETED");
                // delete dir from the backup directory
                char* file_path = src2target_path(event_path, src, target);
                rm_dir_recursive(file_path);
                free(file_path);
            }
            else if (event->mask & IN_MOVED_FROM)
            {
                fprintf(logs, "MOVED_FROM (cookie=%u)", event->cookie);
                pending_cookie = event->cookie;
                strncpy(pending_move_path, event_path, sizeof(pending_move_path));
                // delete dir from the backup directory
                char* file_path = src2target_path(event_path, src, target);
                rm_dir_recursive(file_path);
                free(file_path);
            }
            else if (event->mask & IN_MOVED_TO)
            {
                fprintf(logs, "MOVED_TO (cookie=%u)", event->cookie);
                if (event->cookie == pending_cookie && pending_cookie != 0)
                {
                    // update watch_paths
                    update_watch_paths(w, pending_move_path, event_path);
                    // update cookie
                    pending_cookie = 0;
                    pending_move_path[0] = '\0';
                    // copy dir in the backup directory
                    char* file_path = src2target_path(event_path, src, target);
                    if (mkdir(file_path, 0777) < 0)
                    {
                        ERR("mkdir");
                        exit(EXIT_FAILURE);
                    }
                    // copy permissions
                    copy_permissions(event_path, file_path);
                    // copy dir files and subdirs into backup
                    copy_dir(event_path, file_path, src, target, logs);
                    free(file_path);
                }
                else
                {
                    // copy moved dir into backup folder
                    char* new_path = src2target_path(event_path, src, target);
                    if (mkdir(new_path, 0777) < 0)
                    {
                        ERR("mkdir");
                        exit(EXIT_FAILURE);
                    }
                    fprintf(logs, "\n");
                    // copy permissions
                    copy_permissions(event_path, new_path);
                    // copy dir
                    copy_dir(event_path, new_path, src, target, logs);
                    free(new_path);
                    add_watch_recursive(w, strdup(event_path));
                }
            }
            else if (event->mask & IN_ATTRIB)
            {
                fprintf(logs, "ATTRIB_CHANGE");
                char* file_path = src2target_path(event_path, src, target);
                copy_permissions(event_path, file_path);
                free(file_path);
            }
            else if (event->mask & IN_CLOSE_WRITE)
            {
                fprintf(logs, "CLOSE_WRITE");
            }
            fprintf(logs, "\n");
        }
        // handle files
        else
        {
            fprintf(logs, "\tFile '%s' was ", event_path);

            // handle creation, modification and moved to events (copy file)
            if (event->mask & IN_CREATE || event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO)
            {
                if (event->mask & IN_CREATE)
                    fprintf(logs, "CREATED");
                else if (event->mask & IN_CLOSE_WRITE)
                    fprintf(logs, "CLOSE_WRITE");
                else
                    fprintf(logs, "MOVED_TO");

                // check if file is a symlink
                struct stat stat_info;
                char* file_path = src2target_path(event_path, src, target);

                if (lstat(event_path, &stat_info) != 0)
                {
                    ERR("lstat");
                    exit(EXIT_FAILURE);
                }

                // copy file or symlink
                if (S_ISLNK(stat_info.st_mode))
                {
                    fprintf(logs, "\n");
                    copy_symlink(event_path, file_path, src, target, logs);
                }
                else
                {
                    copy_file(event_path, file_path);
                }

                free(file_path);
            }
            // handle deletion and moved from events (delete file)
            if (event->mask & IN_DELETE || event->mask & IN_MOVED_FROM)
            {
                if (event->mask & IN_DELETE)
                    fprintf(logs, "DELETED");
                else
                    fprintf(logs, "MOVED_FROM");

                // delete file in the backup directory
                char* file_path = src2target_path(event_path, src, target);
                if (unlink(file_path) < 0)
                {
                    ERR("unlink");
                    exit(EXIT_FAILURE);
                }
                free(file_path);
            }
            // file attributes changed
            if (event->mask & IN_ATTRIB)
            {
                fprintf(logs, "ATTRIB_CHANGE");
                char* file_path = src2target_path(event_path, src, target);
                copy_permissions(event_path, file_path);
                free(file_path);
            }

            fprintf(logs, "\n");
        }

        // flush logs
        fflush(logs);
        // skip to the next event struct
        i += sizeof(struct inotify_event) + event->len;
        // free
        free(event_path);
    }
}

// change src path to target
char* src2target_path(char* event_path, char* src, char* target)
{
    int src_len = strlen(src);
    char* inside_path = event_path + src_len + 1;

    return join_paths(target, inside_path);
}

// copy file permissions from file1 to file2 and creations times
void copy_permissions(char* file1, char* file2)
{
    struct stat stat_info;

    // get file1 permissions
    if (lstat(file1, &stat_info) != 0)
    {
        ERR("lstat");
        exit(EXIT_FAILURE);
    }
    // set permissions for file2
    if (chmod(file2, stat_info.st_mode) != 0)
    {
        ERR("chmod");
        exit(EXIT_FAILURE);
    }
    // set atime and mtime
    struct timespec times[2];
    times[0] = stat_info.st_atim;  // atime
    times[1] = stat_info.st_mtim;  // mtime

    if (utimensat(AT_FDCWD, file2, times, AT_SYMLINK_NOFOLLOW) < 0)
    {
        ERR("utimensta");
        exit(EXIT_FAILURE);
    }
}
