#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 256

typedef void (*builtin_func)(char *argv[]);
typedef enum { SEEKING, IN_ARG, IN_QUOTE } state_t;

typedef struct {
    char *name;
    builtin_func func;
} builtin_command;

void do_exit(char *argv[]);
void do_echo(char *argv[]);
void do_type(char *argv[]);
void do_pwd(char *argv[]);
void do_cd(char *argv[]);

int read_line(char *input, size_t input_size);
int tokenize(char *args[], char *buf);
char *get_path(char *command);
void run_external_program(char *argv[]);

builtin_command builtins[] = {{"exit", do_exit}, {"echo", do_echo},
                              {"type", do_type}, {"pwd", do_pwd},
                              {"cd", do_cd},     {NULL, NULL}};

int main(void) {
    // Flush after every printf
    setbuf(stdout, NULL);

    char inputs[1024];

    while (1) {
        printf("$ ");

        char *arguments[MAX_ARGS];
        if (read_line(inputs, sizeof(inputs)) == -1) {
            exit(EXIT_FAILURE);
        }

        int arg_count = tokenize(arguments, inputs);
        if (arg_count <= 0) {
            continue;
        }

        int builtin_executed = 0;
        for (int i = 0; builtins[i].name != NULL; i++) {
            if (strcmp(arguments[0], builtins[i].name) == 0) {
                builtins[i].func(arguments);
                builtin_executed = 1;
                break;
            }
        }

        if (!builtin_executed) {
            run_external_program(arguments);
        }
    }

    return 0;
}

void do_exit(char *argv[]) { exit(EXIT_SUCCESS); }

void do_echo(char *argv[]) {
    for (int i = 1; argv[i] != NULL; i++) {
        if (i != 1) {
            putchar(' ');
        }
        printf("%s", argv[i]);
    }
    putchar('\n');
}

void do_type(char *argv[]) {
    if (argv[1] == NULL) {
        return;
    }

    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(argv[1], builtins[i].name) == 0) {
            printf("%s is a shell builtin\n", argv[1]);
            return;
        }
    }

    char *full_path = get_path(argv[1]);
    if (full_path) {
        printf("%s is %s\n", argv[1], full_path);
        free(full_path);
    } else {
        printf("%s: not found\n", argv[1]);
    }
}

void do_pwd(char *argv[]) {
    char *dir = getcwd(NULL, 0);
    if (dir != NULL) {
        printf("%s\n", dir);
        free(dir);
    } else {
        perror("getcwd");
    }
}

void do_cd(char *argv[]) {
    char *target = argv[1];

    if (target == NULL || strcmp(target, "~") == 0) {
        char *home = getenv("HOME");
        if (home == NULL) {
            fprintf(stderr, "HOME not set\n");
        } else if (chdir(home) == -1) {
            fprintf(stderr, "cd: %s: %s\n", home, strerror(errno));
        }
    } else if (chdir(target) == -1) {
        fprintf(stderr, "cd: %s: %s\n", target, strerror(errno));
    }
}

int read_line(char *input, size_t input_size) {
    if (fgets(input, input_size, stdin) == NULL) {
        if (ferror(stdin))
            perror("fgets failed");
        return -1;
    }

    /* remove the trailing newline */
    input[strcspn(input, "\n")] = '\0';
    return 0;
}

/* tokenizes input into an array of strings it returns the number of tokens read
 * a value of -1 indicates error with stdin or EOF reached
 */
int tokenize(char *args[], char *buf) {
    int argc = 0;

    state_t state = SEEKING;

    while (*buf == ' ')
        buf++;

    char *write = buf;

    for (char *current = buf; *current != '\0'; current++) {
        char c = *current;

        switch (state) {
            case SEEKING: {
                if (c == ' ')
                    continue;
                args[argc++] = write;
                if (c == '\'') {
                    state = IN_QUOTE;
                } else {
                    *write++ = c;
                    state = IN_ARG;
                }
                break;
            }
            case IN_ARG: {
                if (c == ' ') {
                    *write++ = '\0';
                    state = SEEKING;
                } else if (c == '\'') {
                    state = IN_QUOTE;
                } else {
                    *write++ = c;
                }
                break;
            }
            case IN_QUOTE: {
                if (c == '\'') {
                    state = IN_ARG;
                } else {
                    *write++ = c;
                }
                break;
            }
        }
    }

    if (state == IN_QUOTE) {
        return -1;
    }

    if (state == IN_ARG) {
        *write = '\0';
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
