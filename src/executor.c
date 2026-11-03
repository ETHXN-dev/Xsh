#include "executor.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

void run_external_program(char *argv[], bool background) {
    char *full_path = get_path(argv[0]);
    if (full_path == NULL) {
        fprintf(stderr, "%s: command not found\n", argv[0]);
        return;
    }

    // fork the program to allow external programs to run
    pid_t pid = fork();

    if (pid < 0) {
        /* pid < 0 means fork() failed */
        perror("fork");
        free(full_path);
        return;
    } else if (pid == 0) {
        /*  inside the child process,
         * replace the child process with the external program */
        execv(full_path, argv);
        // execv only returns if there was a failure
        perror("execv");
        exit(127);
    } else {
        // inside the parent process
        if (!background) {
            int status;
            /* wait for the child process to finish running */
            waitpid(pid, &status, 0);
        } else {
            printf("[%d] %d\n", Highest_job_number, pid);
            Running_jobs[Highest_job_number].pid = pid;
        }
    }

    free(full_path);
}

void execute_command(char *argv[]) {
    for (int i = 0; Builtins[i].name != NULL; i++) {
        if (strcmp(argv[0], Builtins[i].name) == 0) {
            Builtins[i].func(argv);
            return;
        }
    }

    run_external_program(argv, false);
}

bool valid_pipeline(char *argv[]) {
    for (int i = 1; argv[i] != NULL; i++) {
        if (strcmp(argv[i], "|") == 0 &&
            (argv[i + 1] == NULL || strcmp(argv[i - 1], "|") == 0)) {
            fprintf(stderr, "syntax error near unexpected token `|'\n");
            return false;
        }
    }
    return true;
}

void run_pipeline(char *argv[]) {
    char **cmds[MAX_ARGS];
    int ncmds = 0;

    cmds[ncmds++] = &argv[0];
    for (int i = 1; argv[i] != NULL; i++) {
        if (strcmp(argv[i], "|") == 0) {
            argv[i] = NULL;
            cmds[ncmds++] = &argv[i + 1];
        }
    }

    int pipes[MAX_ARGS][2];
    int npipes = ncmds - 1;
    for (int i = 0; i < npipes; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            for (int j = 0; j < i; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            return;
        }
    }

    pid_t pids[MAX_ARGS];
    int forked = 0;

    for (int i = 0; i < ncmds; i++) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            break;
        } else if (pid == 0) {
            if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                perror("dup2");
                _exit(1);
            }
            if (i < ncmds - 1 && dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                perror("dup2");
                _exit(1);
            }

            /* Child must close every pipe fd, or downstream readers
             * never see EOF */
            for (int j = 0; j < npipes; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            run_in_child(cmds[i]);
        }

        pids[forked++] = pid;
    }

    /* Parent closes all its copies so EOF propagates */
    for (int i = 0; i < npipes; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    for (int i = 0; i < forked; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
}

void run_in_child(char *argv[]) {
    for (int i = 0; Builtins[i].name != NULL; i++) {
        if (strcmp(argv[0], Builtins[i].name) == 0) {
            Builtins[i].func(argv);
            _exit(0);
        }
    }
    execvp(argv[0], argv);
    fprintf(stderr, "%s: command not found\n", argv[0]);
    _exit(127);
}
