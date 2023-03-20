
#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_LEN 4096

static void handle_event(int inotify_instance, int *watched, char **filenames,
                         int *counter, size_t watched_len);
static char **filenames_in_dir(char *path, size_t *return_len);
static char **filenames_in_path_envvar(size_t *return_len);
static void free_null_terminated_pointer_array(char **ptr);
static void print_event(uint32_t event);
static void save_result(char **filenames, int *counter, size_t arr_len);

/* This code is a bit over-commented in general  */
int main(int argc, char *const argv[])
{
    /* Get null terminated list of files to watch, create counter of same size
     */

    char **file_names;
    int *use_counter, *watched;
    size_t files_len;

    switch (argc) {
    case 1:
        file_names = filenames_in_path_envvar(&files_len);
        break;
    case 2:
        file_names = filenames_in_dir(argv[1], &files_len);
        break;
    default:
        errx(EXIT_FAILURE,
             "Usage: `%s <dir>`. If dir is empty, directories in PATH will be "
             "monitored",
             argv[0]);
    }

    if (file_names == NULL)
        err(EXIT_FAILURE, "Error when finding files");

    use_counter = calloc(files_len, sizeof(int));

    if (use_counter == NULL)
        err(EXIT_FAILURE, "Failed to allocate %zu bytes for use counter",
            files_len * sizeof(int));

    /* Create file descriptor for accessing the inotify API */

    int inotify_instance = inotify_init();

    if (inotify_instance == -1)
        err(EXIT_FAILURE, "Failed to initialize inotify instance");

    /* Mark files for events */

    watched = calloc(files_len, sizeof(int));

    if (watched == NULL)
        err(EXIT_FAILURE, "Failed to allocate %zu bytes for watch descriptors",
            files_len * sizeof(int));

    for (size_t i = 0; file_names[i] != NULL; i++) {
        watched[i] =
            inotify_add_watch(inotify_instance, file_names[i], IN_ACCESS);

        if (watched[i] == -1)
            err(EXIT_FAILURE, "Cannot watch %s", file_names[i]);
    }

    /* Prepare for polling */

    struct pollfd poll_fds[2];
    nfds_t fd_count = 2;

    poll_fds[0].fd = STDIN_FILENO;
    poll_fds[0].events = POLLIN;

    poll_fds[1].fd = inotify_instance;
    poll_fds[1].events = POLLIN;

    /* Wait for events and/or terminal input */

    int poll_num;

    fprintf(stderr, "Listening for events... (Press enter to end)\n");

    while (1) {
        poll_num = poll(poll_fds, fd_count, -1);

        if (poll_num == -1 && errno != EINTR)
            err(EXIT_FAILURE, "Failed to poll");

        if (poll_num == 0)
            continue;

        if (poll_fds[0].revents & POLLIN) {
            char c;
            while (read(STDIN_FILENO, &c, 1) > 0 && c != '\n')
                /* noop */;
            save_result(file_names, use_counter, files_len);
            break;
        }

        if (poll_fds[1].revents & POLLIN) {
            handle_event(inotify_instance, watched, file_names, use_counter,
                         files_len);
        }
    }

    close(inotify_instance);

    free_null_terminated_pointer_array(file_names);
    free(watched);
    free(use_counter);

    return EXIT_SUCCESS;
}

/* Free allocated memory pointed to by pointers
   in 'ptr', then free 'ptr' itself */
static void free_null_terminated_pointer_array(char **ptr)
{
    for (char **p = ptr; *p != NULL; p++) {
        free(*p);
    }

    free(ptr);
}

/* Returns a null terminated array of strings
   for every file in PATH */
static char **filenames_in_path_envvar(size_t *return_len)
{
    size_t n_files, i, output_size;
    char **filenames, **output, *saveptr, *path, *token;
    void *tmp;

    path = strdup(getenv("PATH"));

    saveptr = NULL;
    output_size = 1024;
    output = malloc(output_size * sizeof(char *));

    if (output == NULL) {
        warn("Failed to allocate %zu bytes", output_size);
        return NULL;
    }

    token = strtok_r(path, ":", &saveptr);

    i = 0;
    while (token != NULL) {
        n_files = 0;
        filenames = filenames_in_dir(token, &n_files);
        n_files -= 1;

        if (filenames == NULL) {
            fprintf(stderr, "failed to get filenames in %s\n", token);
            token = strtok_r(NULL, ":", &saveptr);
            continue;
        }

        if (i + n_files > output_size) {
            while (i + n_files > output_size)
                output_size *= 2;

            tmp = realloc(output, output_size * sizeof(char *));

            if (tmp == NULL) {
                warn("failed to reallocate %zu bytes",
                     output_size * sizeof(char *));
                return NULL;
            }

            output = (char **)tmp;
        }

        memcpy(output + i, filenames, n_files * sizeof(char *));
        i += n_files;

        free(filenames);

        token = strtok_r(NULL, ":", &saveptr);
    }
    output_size = i + 1;
    tmp = realloc(output, output_size * sizeof(char *));

    if (tmp != NULL) {
        output = (char **)tmp;
    }

    output[i] = NULL;

    free(path);

    *return_len = output_size;

    return output;
}

/* Returns null terminated array
   of filepaths in directory */
static char **filenames_in_dir(char *path, size_t *return_len)
{
    size_t result_size, n_items = 0;
    char **result;
    void *tmp;
    struct dirent *dir_entity;
    DIR *directory_stream;

    result_size = 64;
    result = malloc(result_size * sizeof(char *));

    if (result == NULL) {
        warn("Failed to malloc %zu bytes", result_size);
        return NULL;
    }

    directory_stream = opendir(path);

    if (directory_stream == NULL) {
        warn("Failed to open directory %s", path);
        goto fail;
    }

    n_items = 0;
    errno = 0; // To distinguish end of stream from an error,
               // set errno to zero before calling readdir()
    while ((dir_entity = readdir(directory_stream)) != NULL) {
        if (n_items >= result_size) {
            result_size *= 2;
            result = realloc(result, result_size * sizeof(char *));
        }

        if (dir_entity->d_type == DT_REG) {
            result[n_items] = calloc(
                strlen(path) + strlen(dir_entity->d_name) + 2, sizeof(char));
            result[n_items] = strcat(result[n_items], path);
            result[n_items] = strcat(result[n_items], "/");
            result[n_items] = strcat(result[n_items], dir_entity->d_name);
            n_items += 1;
        }

        // dirent *dir_entity is statically allocated, dont free()!
    }
    result_size = n_items + 1;
    tmp = realloc(result, result_size * sizeof(char *));

    if (tmp == NULL) {
        warn("Failed to realloc %zu bytes", result_size);
        return NULL;
    }

    result = (char **)tmp;

    result[n_items] = NULL;

    if (errno != 0) {
        warn("Error when reading directory %s", path);
        goto fail;
    }

    if (closedir(directory_stream) == -1)
        warn("Failed to close directory");

    *return_len = result_size;

    return result;

fail:
    for (size_t i = 0; i < n_items; i++) {
        free(result[i]);
    }
    free(result);

    if (closedir(directory_stream) == -1)
        warn("Failed to close directory");

    *return_len = 0;

    return NULL;
}

/* Read all available inotify events from
   file descriptor 'inotify_instance' */
static void handle_event(int inotify_instance, int *watched, char **filenames,
                         int *counter, size_t watched_len)
{
    ssize_t length;
    const struct inotify_event *event;
    char buffer[BUFFER_LEN]
        __attribute__((aligned(__alignof__(struct inotify_event))));

    length = read(inotify_instance, buffer, BUFFER_LEN);

    if (length == -1 && errno != EAGAIN) {
        warn("Failed to read inotify event");
        return;
    }

    if (length == 0) {
        return;
    }

    for (char *p = buffer; p < buffer + length;
         p += sizeof(struct inotify_event) + event->len) {
        event = (struct inotify_event *)p;

        for (size_t i = 0; i < watched_len; i++) {
            if (watched[i] == event->wd) {
                fprintf(stderr, "%s ", filenames[i]);
                counter[i] += 1;
                break;
            }
        }
    }
}


#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
/* Prints event mask values in a human-readable format */
static void print_event(uint32_t event)
{
	UNUSED(print_event);

    if (event & IN_ACCESS) {
        fprintf(stderr, "IN_ACCESS ");
    }
    if (event & IN_ATTRIB) {
        fprintf(stderr, "IN_ATTRIB ");
    }
    if (event & IN_CLOSE_WRITE) {
        fprintf(stderr, "IN_CLOSE_WRITE ");
    }
    if (event & IN_CLOSE_NOWRITE) {
        fprintf(stderr, "IN_CLOSE_NOWRITE ");
    }
    if (event & IN_CREATE) {
        fprintf(stderr, "IN_CREATE ");
    }
    if (event & IN_DELETE) {
        fprintf(stderr, "IN_DELETE ");
    }
    if (event & IN_DELETE_SELF) {
        fprintf(stderr, "IN_DELETE_SELF ");
    }
    if (event & IN_MODIFY) {
        fprintf(stderr, "IN_MODIFY ");
    }
    if (event & IN_MOVE_SELF) {
        fprintf(stderr, "IN_MOVE_SELF ");
    }
    if (event & IN_MOVED_FROM) {
        fprintf(stderr, "IN_MOVED_FROM ");
    }
    if (event & IN_OPEN) {
        fprintf(stderr, "IN_OPEN ");
    }
}
#pragma GCC diagnostic pop


/* Saves number of recorded events for each
   executable to 'uses.log' */
static void save_result(char **filenames, int *counter, size_t arr_len)
{
    FILE *savefile;

    savefile = fopen("uses.log", "w");

    if (savefile == NULL) {
        warn("Failed to save to file");
        return;
    }

    for (size_t i = 0; i < arr_len; i++) {
        if (counter[i] == 0)
            continue;

        fprintf(savefile, "%d %s\n", counter[i], filenames[i]);
    }

    fclose(savefile);
}
