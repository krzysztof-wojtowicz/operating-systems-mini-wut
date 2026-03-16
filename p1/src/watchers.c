#include "watchers.h"

// init inotify and watchers struct
Watchers* watchers_init()
{
    Watchers* new_dict = malloc(sizeof(Watchers));
    if (new_dict == NULL)
    {
        ERR("malloc");
        exit(EXIT_FAILURE);
    }

    new_dict->head = NULL;
    new_dict->size = 0;

    // init inotify
    new_dict->fd = inotify_init();
    if (new_dict->fd < 0)
    {
        ERR("inotify_init");
        exit(EXIT_FAILURE);
    }

    return new_dict;
}

// free watchers struct
void free_watchers(Watchers* w)
{
    if (w == NULL)
        return;

    Watch* p = w->head;
    // free watches
    while (p != NULL)
    {
        Watch* next = p->next;
        // removes watches
        if (inotify_rm_watch(w->fd, p->wd) != 0)
        {
            ERR("inotify_rm_watch");
            exit(EXIT_FAILURE);
        }
        free(p->path);
        free(p);
        p = next;
    }

    // close inotify
    if (close(w->fd) < 0)
    {
        ERR("close");
        exit(EXIT_FAILURE);
    }

    free(w);
}

// create new watch, returns wd
int add_watch(Watchers* w, char* path)
{
    uint32_t mask = IN_CREATE | IN_DELETE | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;

    // add watcher
    int wd = inotify_add_watch(w->fd, path, mask);
    if (wd < 0)
    {
        ERR("inotify_add_watch");
        exit(EXIT_FAILURE);
    }

    // create new Watch object
    Watch* new_watch = malloc(sizeof(Watch));
    if (new_watch == NULL)
    {
        ERR("new_watch");
        exit(EXIT_FAILURE);
    }

    // add it to dict
    new_watch->path = path;
    new_watch->wd = wd;
    new_watch->next = w->head;
    w->head = new_watch;
    w->size++;

    // return wd
    return wd;
}

// delete watch from dict
void delete_watch(Watchers* w, int wd)
{
    if (w == NULL)
        return;

    Watch* p = w->head;
    // if wd at the beginning of list
    if (p->wd == wd)
    {
        w->head = p->next;
        free(p->path);
        free(p);
        w->size--;
        return;
    }

    p = p->next;
    Watch* prev = w->head;
    // search for wd and delete its Watch
    while (p != NULL)
    {
        if (p->wd == wd)
        {
            prev->next = p->next;
            free(p->path);
            free(p);
            w->size--;
            return;
        }
        prev = p;
        p = p->next;
    }
}

// searches for Watch for given wd
Watch* search_watch(Watchers* w, int wd)
{
    if (w == NULL)
        return NULL;

    Watch* p = w->head;
    // search for Watch
    while (p != NULL)
    {
        if (p->wd == wd)
        {
            return p;
        }
        p = p->next;
    }

    return NULL;
}

// prints Watchers to logs
void print_watchers(Watchers* w, FILE* logs, char* src, char* target)
{
    fprintf(logs, "[%d] INOTIFY STATUS\n", getpid());
    if (w == NULL)
    {
        fprintf(logs, "\tNo active inotify.\n");
        return;
    }
    fprintf(logs, "\tInotify [%d], %d watchers:\n", w->fd, w->size);

    Watch* p = w->head;

    while (p != NULL)
    {
        fprintf(logs, "\t - watcher [%d] for %s\n", p->wd, p->path);
        p = p->next;
    }
    fflush(logs);
}

// add watches recursively
void add_watch_recursive(Watchers* w, char* path)
{
    DIR* dir = opendir(path);
    if (dir == NULL)
    {
        ERR("opendir");
        exit(EXIT_FAILURE);
    }

    // add main dir
    add_watch(w, path);

    struct dirent* file_info;
    struct stat stat_info;

    // search for subdirs
    while ((file_info = readdir(dir)) != NULL)
    {
        // ignore "." and ".."
        if (strcmp(file_info->d_name, ".") == 0 || strcmp(file_info->d_name, "..") == 0)
        {
            continue;
        }
        // get file path
        char* file_path = join_paths(path, file_info->d_name);
        // get stat
        if (lstat(file_path, &stat_info) != 0)
        {
            ERR("lstat");
            exit(EXIT_FAILURE);
        }

        // if dir run add_watch_recursive()
        if (S_ISDIR(stat_info.st_mode))
        {
            add_watch_recursive(w, file_path);
        }
        else
        {
            free(file_path);
        }
    }

    if (closedir(dir) < 0)
    {
        ERR("closedir");
        exit(EXIT_FAILURE);
    }
}

// updates watch paths
void update_watch_paths(Watchers* w, const char* old_path, const char* new_path)
{
    size_t old_len = strlen(old_path);

    Watch* p = w->head;

    while (p != NULL)
    {
        if (strncmp(p->path, old_path, old_len) == 0 && (p->path[old_len] == '/' || p->path[old_len] == '\0'))
        {
            char new_full_path[PATH_MAX];
            snprintf(new_full_path, sizeof(new_full_path), "%s%s", new_path, p->path + old_len);
            free(p->path);
            p->path = strdup(new_full_path);
        }

        p = p->next;
    }
}
