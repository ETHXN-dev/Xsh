#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ARGS 256
#define MAX_CMD_LEN 1024
#define MAX_COMPLETIONS 1024

#define IS_DOT_OR_DOTDOT(s)                                                    \
    (s[0] == '.' && ((s[1] == '\0') || (s[1] == '.' && s[2] == '\0')))

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
    int fd;
    bool append;
} redirect_type_t;

typedef struct {
    char *name;
    builtin_func func;
} builtin_command;

typedef struct {
    char command[MAX_CMD_LEN];
    char script_path[PATH_MAX];
} completion_register_t;

void do_exit(char *argv[]);
void do_echo(char *argv[]);
void do_type(char *argv[]);
void do_pwd(char *argv[]);
void do_cd(char *argv[]);
void do_complete(char *argv[]);

int tokenize(char *args[], char *buf);
void print_tokenize_error(int err);
char *get_path(char *command);
void run_external_program(char *argv[]);
void execute_command(char *argv[]);

redirect_type_t *classify_redirect(const char *s);
void redirect_stream(char *argv[], char *filename, int stream, int append);

char *command_generator(const char *text, int state);
char **my_completion(const char *text, int start, int end);

builtin_command builtins[] = {{"exit", do_exit}, {"echo", do_echo},
                              {"type", do_type}, {"pwd", do_pwd},
                              {"cd", do_cd},     {"complete", do_complete},
                              {NULL, NULL}};

redirect_type_t redirect_types[] = {{">", STDOUT_FILENO, false},
                                    {"1>", STDOUT_FILENO, false},
                                    {"2>", STDERR_FILENO, false},
                                    {">>", STDOUT_FILENO, true},
                                    {"1>>", STDOUT_FILENO, true},
                                    {"2>>", STDERR_FILENO, true},
                                    {NULL, 0, false}};

/* Stores completions entered by user.
 * Last slot is intentionally empty as a sentinel
 * Usable capacity is therefore MAX_COMPLETIONS - 1, not MAX_COMPLETIONS.
 */
completion_register_t Completions_registered[MAX_COMPLETIONS];

int main(void) {
    // Flush after every printf
    setbuf(stdout, NULL);

    rl_attempted_completion_function = my_completion;

    while (1) {
        char *inputs = readline("$ ");
        if (inputs == NULL) {
            exit(EXIT_SUCCESS);
        }

        char *arguments[MAX_ARGS];

        int arg_count = tokenize(arguments, inputs);
        if (arg_count == 0) {
            free(inputs);
            continue; // blank line, nothing to do
        }
        if (arg_count < 0) {
            print_tokenize_error(arg_count);
            free(inputs);
            continue;
        }

        redirect_type_t *redirect = NULL;
        char *filename = NULL;
        int idx;
        for (idx = 0; arguments[idx] != NULL; idx++) {
            redirect = classify_redirect(arguments[idx]);
            if (redirect != NULL) {
                arguments[idx] = NULL;
                filename = arguments[idx + 1];
                break;
            }
        }

        if (idx == 0) {
            fprintf(stderr, "syntax error: expected command\n");
            free(inputs);
            continue;
        }

        if (redirect != NULL) {
            if (filename == NULL) {
                // user didn't enter a file to redirect to
                fprintf(stderr,
                        "syntax error near unexpected token `newline'\n");
                free(inputs);
                continue;
            } else {
                redirect_stream(arguments, filename, redirect->fd,
                                redirect->append);
            }
        } else {
            execute_command(arguments);
        }
        free(inputs);
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

void do_complete(char *argv[]) {
    if (argv[1] == NULL) {
        return;
    }

    if (strcmp(argv[1], "-p") == 0) {
        if (argv[2] == NULL) {
            return;
        }

        char *command = argv[2];
        for (int i = 0; Completions_registered[i].command[0] != '\0'; i++) {
            if (strcmp(command, Completions_registered[i].command) == 0) {
                printf("complete -C '%s' %s\n",
                       Completions_registered[i].script_path, command);
                return;
            }
        }
        printf("complete: %s: no completion specification\n", argv[2]);

    } else if (strcmp(argv[1], "-C") == 0) {
        if (argv[3] == NULL || argv[2] == NULL) {
            return;
        }

        char *script_path = argv[2];
        char *command = argv[3];
        int idx = 0;

        for (idx = 0; idx < MAX_COMPLETIONS - 1; idx++) {
            if (Completions_registered[idx].command[0] == '\0') {
                break;
            }

            if (strcmp(Completions_registered[idx].command, command) == 0) {
                strncpy(Completions_registered[idx].script_path, script_path,
                        PATH_MAX);
                Completions_registered[idx].script_path[PATH_MAX - 1] = '\0';
                return;
            }
        }

        if (idx == MAX_COMPLETIONS - 1) {
            fprintf(stderr, "shell can only contain %d completions\n",
                    MAX_COMPLETIONS - 1);
            return;
        }

        strncpy(Completions_registered[idx].command, command, MAX_CMD_LEN);
        Completions_registered[idx].command[MAX_CMD_LEN - 1] = '\0';

        strncpy(Completions_registered[idx].script_path, script_path, PATH_MAX);
        Completions_registered[idx].script_path[PATH_MAX - 1] = '\0';
    }
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

void print_tokenize_error(int err) {
    switch (err) {
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

redirect_type_t *classify_redirect(const char *s) {
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

/* Completion generator for GNU readline's rl_completion_matches().
 * Readline calls this repeatedly with the same partial `text`,
 * incrementing `state` each time; state == 0 signals a new completion attempt,
 * so we reset our position in `builtins`.
 * Returns a strdup'd name of the next builtin whose name starts with `text`,
 * or NULL when no more matches remain. */
char *command_generator(const char *text, int state) {
    static int index;
    static size_t len;
    static char *path_copy;
    static char *dir_token;

    if (state == 0) {
        index = 0;
        len = strlen(text);

        path_copy = getenv("PATH");
        if (path_copy == NULL) {
            fprintf(stderr, "PATH not set\n");
            return NULL;
        }

        path_copy = strdup(path_copy);
        if (path_copy == NULL) {
            perror("strdup");
            return NULL;
        }

        dir_token = strtok(path_copy, ":");
    }

    while (builtins[index].name != NULL) {
        char *name = builtins[index].name;
        index++;

        if (strncmp(text, name, len) == 0) {
            return strdup(name);
        }
    }

    while (dir_token != NULL) {
        static DIR *current_dir;

        if (current_dir == NULL) {
            current_dir = opendir(dir_token);

            if (current_dir == NULL) {
                fprintf(stderr, "opendir(%s): %s\n", dir_token,
                        strerror(errno));
                dir_token = strtok(NULL, ":");
                continue;
            }
        }

        struct dirent *entry;
        while ((entry = readdir(current_dir)) != NULL) {
            if (IS_DOT_OR_DOTDOT(entry->d_name)) {
                continue;
            }

            if (strncmp(text, entry->d_name, len) == 0) {
                return strdup(entry->d_name);
            }
        }

        closedir(current_dir);
        current_dir = NULL;

        dir_token = strtok(NULL, ":");
    }

    free(path_copy);

    return NULL;
}

/* Custom completion function for GNU readline (set via
 * rl_attempted_completion_function). Called once per completion attempt with
 * the full line context: `text` is the word being completed, `start`/`end` are
 * its offsets into rl_line_buffer. We only offer builtin-name completions when
 * the word being completed is the first word on the line (start == 0);
 * otherwise we return NULL so readline falls back to its default filename
 * completion. */
char **my_completion(const char *text, int start, int end) {
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    }
    return NULL;
}
