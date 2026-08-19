#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 256

typedef void (*builtin_func)(char *argv[]);
typedef enum {
    SEEKING,
    IN_ARG,
    IN_SINGLE_QUOTE,
    IN_DOUBLE_QUOTE,
    ESCAPING
} state_t;

typedef enum {
    TOKENIZE_ERR_TOO_MANY_ARGS = -1,
    TOKENIZE_ERR_UNTERMINATED_QUOTE = -2,
    TOKENIZE_ERR_TRAILING_ESCAPE = -3,
} tokenize_err_t;

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
void execute_command(char *argv[]);
void redirect_stream(char *argv[], char *filename, int stream);

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
        if (arg_count == 0) {
            continue; // blank line, nothing to do
        }
        if (arg_count < 0) {
            switch (arg_count) {
                case TOKENIZE_ERR_TOO_MANY_ARGS:
                    fprintf(stderr, "shell: too many arguments\n");
                    break;
                case TOKENIZE_ERR_UNTERMINATED_QUOTE:
                    fprintf(stderr, "shell: unterminated quote\n");
                    break;
                case TOKENIZE_ERR_TRAILING_ESCAPE:
                    fprintf(stderr, "shell: trailing backslash\n");
                    break;
            }
            continue;
        }

        bool redirected_stdout = false;
        bool redirected_stderr = false;
        char *filename = NULL;
        for (int i = 0; arguments[i] != NULL; i++) {
            if ((strcmp(arguments[i], ">") == 0) ||
                (strcmp(arguments[i], "1>") == 0)) {
                redirected_stdout = true;
                arguments[i] = NULL;
                filename = arguments[i + 1];
                break;
            } else if (strcmp(arguments[i], "2>") == 0) {
                redirected_stderr = true;
                arguments[i] = NULL;
                filename = arguments[i + 1];
                break;
            }
        }

        // user didn't enter a file to redirect to
        if ((redirected_stdout || redirected_stderr) && (filename == NULL)) {
            fprintf(stderr, "syntax error near unexpected token `newline'\n");
            return 1;
        }

        if (redirected_stdout) {
            redirect_stream(arguments, filename, STDOUT_FILENO);
        } else if (redirected_stderr) {
            redirect_stream(arguments, filename, STDERR_FILENO);
        } else {
            execute_command(arguments);
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
        fprintf(stderr, "%s: not found\n", argv[1]);
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

/*
 * tokenize: splits buf into whitespace-separated tokens, honoring
 * single and double quotes. WARNING: mutates buf in place — args[]
 * entries point directly into buf's storage, which is overwritten
 * with '\0' separators and de-quoted content.
 *
 * Invariant: write always trails or equals current, since quote
 * chars and delimiters are consumed without being copied.
 *
 * Returns argc on success, -1 on unterminated quote or too many args.
 */
int tokenize(char *args[], char *buf) {
    int argc = 0;

    state_t state = SEEKING;

    while (isspace((unsigned)*buf))
        buf++;

    char *write = buf;

    for (char *current = buf; *current != '\0'; current++) {
        char c = *current;

        switch (state) {
            case SEEKING: {
                if (isspace((unsigned)c))
                    continue;
                if (argc >= MAX_ARGS - 1) {
                    args[0] = NULL;
                    return TOKENIZE_ERR_TOO_MANY_ARGS;
                }
                args[argc++] = write;
                if (c == '\'') {
                    state = IN_SINGLE_QUOTE;
                } else if (c == '"') {
                    state = IN_DOUBLE_QUOTE;
                } else if (c == '\\') {
                    state = ESCAPING;
                } else {
                    *write++ = c;
                    state = IN_ARG;
                }
                break;
            }
            case IN_ARG: {
                if (isspace((unsigned)c)) {
                    *write++ = '\0';
                    state = SEEKING;
                } else if (c == '\'') {
                    state = IN_SINGLE_QUOTE;
                } else if (c == '"') {
                    state = IN_DOUBLE_QUOTE;
                } else if (c == '\\') {
                    state = ESCAPING;
                } else {
                    *write++ = c;
                }
                break;
            }
            case IN_SINGLE_QUOTE: {
                if (c == '\'') {
                    state = IN_ARG;
                } else {
                    *write++ = c;
                }
                break;
            }
            case IN_DOUBLE_QUOTE: {
                if (c == '"') {
                    state = IN_ARG;
                } else if (c == '\\') {
                    switch (*(current + 1)) {
                        case '\"':
                        case '\\': {
                            *write++ = *(current + 1);
                            current++;
                            break;
                        }
                        default: {
                            *write++ = c;
                            break;
                        }
                    }
                } else {
                    *write++ = c;
                }
                break;
            }
            case ESCAPING: {
                *write++ = c;
                state = IN_ARG;
                break;
            }
        }
    }

    if (state == IN_SINGLE_QUOTE || state == IN_DOUBLE_QUOTE) {
        args[0] = NULL;
        return TOKENIZE_ERR_UNTERMINATED_QUOTE;
    }

    if (state == ESCAPING) {
        args[0] = NULL;
        return TOKENIZE_ERR_TRAILING_ESCAPE;
    }

    if (state == IN_ARG)
        *write = '\0';

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

void execute_command(char *argv[]) {
    for (int i = 0; builtins[i].name != NULL; i++) {
        if (strcmp(argv[0], builtins[i].name) == 0) {
            builtins[i].func(argv);
            return;
        }
    }

    run_external_program(argv);
}

void redirect_stream(char *argv[], char *filename, int stream) {
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
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
