#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 10

int get_input(char *args[], char *buf, size_t buf_size);
char *get_path(char *command);
void run_external_program(char *argv[]);

int main(void) {
    // Flush after every printf
    setbuf(stdout, NULL);

    char inputs[1024];

    while (1) {
        printf("$ ");

        char *arguments[MAX_ARGS];
        int arg_count = get_input(arguments, inputs, sizeof(inputs));

        if (arg_count < 0) {
            putchar('\n');
            break; // Any negative return code means stop execution
        } else if (arg_count == 0) {
            continue;
        }

        if (strcmp(arguments[0], "exit") == 0) {
            break;
        } else if (strcmp(arguments[0], "echo") == 0) {
            /* print the arguments entered after echo */
            for (int i = 1; i < arg_count; i++)
                printf("%s ", arguments[i]);
            putchar('\n');
        } else if (strcmp(arguments[0], "type") == 0) {
            if (!arguments[1])
                continue;

            if (strcmp(arguments[1], "type") == 0 ||
                strcmp(arguments[1], "exit") == 0 ||
                strcmp(arguments[1], "echo") == 0 ||
                strcmp(arguments[1], "pwd") == 0) {
                printf("%s is a shell builtin\n", arguments[1]);
                continue;
            } else {
                char *full_path = get_path(arguments[1]);

                if (full_path) {
                    printf("%s is %s\n", arguments[1], full_path);
                    free(full_path);
                } else {
                    printf("%s: not found\n", arguments[1]);
                }
            }

        } else if (strcmp(arguments[0], "pwd") == 0) {
            char *dir = getcwd(NULL, 0);
            if (dir != NULL) {
                printf("%s\n", dir);
                free(dir);
            } else {
                perror("getcwd");
                exit(EXIT_FAILURE);
            }

        } else {
            run_external_program(arguments);
        }
    }

    return 0;
}

/* reads input from user into buf and tokenizes it into an array of strings
 * it returns the number of tokens read a value of -1 indicates error with
 * stdin or EOF reached
 */
int get_input(char *args[], char *buf, size_t buf_size) {
    if (fgets(buf, buf_size, stdin) == NULL) {
        if (ferror(stdin))
            perror("fgets failed");
        return -1;
    }

    /* remove the trailing newline */
    buf[strcspn(buf, "\n")] = '\0';

    /* tokenize the input to become arguments */
    int argc = 0;
    char *token = strtok(buf, " ");
    while (token != NULL && argc < MAX_ARGS - 1) {
        args[argc] = token;
        token = strtok(NULL, " ");
        argc++;
    }

    args[argc] = NULL;

    return argc;
}

char *get_path(char *command) {
    char *full_path = NULL;

    char *path_val = getenv("PATH");
    if (!path_val)
        return NULL;

    path_val = strdup(path_val);
    if (!path_val)
        return NULL;

    char *dir = strtok(path_val, ":");
    while (dir) {
        size_t full_len = strlen(command) + strlen(dir) + 2;
        char *candidate = malloc(full_len);
        if (!candidate) {
            free(path_val);
            return NULL;
        }

        snprintf(candidate, full_len, "%s/%s", dir, command);

        if (access(candidate, X_OK) == 0) {
            full_path = candidate;
            break;
        }

        free(candidate);
        dir = strtok(NULL, ":");
    }

    free(path_val);
    return full_path;
}

void run_external_program(char *argv[]) {
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
        int status;
        /* wait for the child process to finish running */
        waitpid(pid, &status, 0);
    }

    free(full_path);
}
