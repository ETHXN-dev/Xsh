#include "redirect.h"
#include "executor.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

RedirectType redirect_types[] = {{">", STDOUT_FILENO, false},
                                 {"1>", STDOUT_FILENO, false},
                                 {"2>", STDERR_FILENO, false},
                                 {">>", STDOUT_FILENO, true},
                                 {"1>>", STDOUT_FILENO, true},
                                 {"2>>", STDERR_FILENO, true},
                                 {NULL, 0, false}};

RedirectType *classify_redirect(const char *s) {
    for (int i = 0; redirect_types[i].name != NULL; i++) {
        if (strcmp(s, redirect_types[i].name) == 0) {
            return &redirect_types[i];
        }
    }
    return NULL;
}

void redirect_stream(char *argv[], char *filename, int stream, int append) {
    // duplicate stream file descriptor to revert redirection later
    int saved_stream = dup(stream);
    if (saved_stream == -1) {
        perror("dup");
        return;
    }

    // get fd with flags O_WRONLY for writing data
    //                   O_CREAT for file creation
    //                   O_TRUNC to right over existing data
    // mode 6 (User): Read (4) + Write (2) = RW
    //      4 (Group): Read (4) = R
    //      4 (Other): Read (4) = R
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    int fd = open(filename, flags, 0644);
    if (fd == -1) {
        perror("open");
        close(saved_stream);
        return;
    }

    if ((dup2(fd, stream)) == -1) {
        perror("dup2 redirect");
        close(fd);
        close(saved_stream);
        return;
    }

    close(fd);

    execute_command(argv);

    if ((dup2(saved_stream, stream)) == -1) {
        perror("dup2 restore");
        close(saved_stream);
        return;
    }

    close(saved_stream);
}
