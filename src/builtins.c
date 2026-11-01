#include "builtins.h"
#include <ctype.h>
#include <errno.h>
#include <readline/history.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

Builtin Builtins[] = {{"exit", do_exit},       {"echo", do_echo},
                      {"type", do_type},       {"pwd", do_pwd},
                      {"cd", do_cd},           {"complete", do_complete},
                      {"jobs", do_jobs},       {"history", do_history},
                      {"declare", do_declare}, {NULL, NULL}};

/* Stores completions entered by user.
 * Last slot is intentionally empty as a sentinel
 * Usable capacity is therefore MAX_COMPLETIONS - 1, not MAX_COMPLETIONS.
 */
Completion Completions_registered[MAX_COMPLETIONS];

int Highest_job_number = 0;
int Running_jobs_count = 0;
Job Running_jobs[MAX_JOBS];

int Persisted_history = 0;

int VariableCount = 0;
ShellVariable Variables[MAX_SHELL_VARIABLES];

void do_exit(char *argv[]) {
    const char *histfile = getenv("HISTFILE");
    if (histfile != NULL) {
        write_history(histfile);
    }
    exit(EXIT_SUCCESS);
}

void do_echo(char *argv[]) {
    for (int i = 1; argv[i] != NULL; i++) {
        if (i != 1) {
            putchar(' ');
        }
        printf("%s", argv[i]);
    }
    putchar('\n');
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

void do_type(char *argv[]) {
    if (argv[1] == NULL) {
        return;
    }

    for (int i = 0; Builtins[i].name != NULL; i++) {
        if (strcmp(argv[1], Builtins[i].name) == 0) {
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
    } else if (strcmp(argv[1], "-r") == 0) {
        if (argv[2] == NULL) {
            return;
        }

        char *command = argv[2];
        for (int i = 0; Completions_registered[i].command[0] != '\0'; i++) {
            if (strcmp(command, Completions_registered[i].command) == 0) {
                Completions_registered[i].command[0] = '\0';
                return;
            }
        }
    }
}

void do_jobs(char *argv[]) {
    usleep(10000); // 10 ms: give just-unblocked children time to exit
    check_jobs(true);
}

void check_jobs(bool display_all) {
    int count = 0, done_jobs = 0, highest_job = 0;
    for (int i = 1; i <= Highest_job_number; i++) {
        if (Running_jobs[i].running_status) {
            count++;
            bool is_done = false;

            int status;
            if ((waitpid(Running_jobs[i].pid, &status, WNOHANG)) ==
                Running_jobs[i].pid) {
                is_done = true;
                Running_jobs[i].running_status = false;
                Running_jobs[i].command_string
                    [strcspn(Running_jobs[i].command_string, "&") - 1] = '\0';
                done_jobs++;
            } else {
                highest_job = i;
            }

            char marker = (count == Running_jobs_count)       ? '+'
                          : (count == Running_jobs_count - 1) ? '-'
                                                              : ' ';

            if (display_all) {
                printf("[%d]%c %-24s%s\n", i, marker,
                       is_done ? "Done" : "Running",
                       Running_jobs[i].command_string);
            } else {
                if (is_done) {
                    printf("[%d]%c %-24s%s\n", i, marker, "Done",
                           Running_jobs[i].command_string);
                }
            }
        }
    }

    Running_jobs_count -= done_jobs;
    Highest_job_number = highest_job;
}

void do_history(char *argv[]) {
    int i = 0;

    if (argv[1] != NULL) {
        if (strcmp(argv[1], "-r") == 0) {
            if (argv[2] == NULL) {
                fprintf(stderr, "usage: history -r <file>\n");
                return;
            }
            read_history(argv[2]);
            return;
        } else if (strcmp(argv[1], "-w") == 0) {
            if (argv[2] == NULL) {
                fprintf(stderr, "usage: history -w <file>\n");
                return;
            }
            write_history(argv[2]);
            return;
        } else if (strcmp(argv[1], "-a") == 0) {
            if (argv[2] == NULL) {
                fprintf(stderr, "usage: history -a <file>\n");
                return;
            }
            int new = history_length - Persisted_history;
            if (new > 0) {
                append_history(new, argv[2]);
                Persisted_history = history_length;
            }
            return;
        } else {
            i = history_length - atoi(argv[1]);
        }
    }

    HIST_ENTRY **list = history_list();
    for (; i < history_length; i++) {
        printf("\t%d  %s\n", i + 1, list[i]->line);
    }
}

size_t var_name_len(char *s) {
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return 0;
    }
    size_t n = 1;
    while (isalnum((unsigned char)s[n]) || s[n] == '_') {
        n++;
    }
    return n;
}

bool has_var_ref(char *s) {
    for (; *s != '\0'; s++) {
        if (*s != '$') {
            continue;
        }
        if (s[1] == '{') {
            size_t n = var_name_len(s + 2);
            if (n > 0 && s[2 + n] == '}') {
                return true;
            }
        } else if (var_name_len(s + 1) > 0) {
            return true;
        }
    }
    return false;
}

int expand_variables(char *argv[], int argc) {
    int kept = 0;

    for (int i = 0; i < argc; i++) {
        char *token = argv[i];

        if (!has_var_ref(token)) {
            argv[kept++] = token; // untouched, still points into inputs
            continue;
        }

        char *buffer = calloc(MAX_ARG_LEN, sizeof(char));
        if (buffer == NULL) {
            perror("calloc");
            argv[kept++] = token; // fall back to the unexpanded token
            continue;
        }

        size_t out = 0;
        const char *p = token;

        while (*p != '\0' && out < MAX_ARG_LEN - 1) {
            if (*p != '$') {
                buffer[out++] = *p++;
                continue;
            }

            /* Work out whether this is ${name} or $name */
            const char *name_start = NULL;
            size_t n = 0;
            size_t consumed = 0;

            if (p[1] == '{') {
                n = var_name_len((char *)p + 2);
                if (n > 0 && p[2 + n] == '}') {
                    name_start = p + 2;
                    consumed = n + 3; /* ${ + name + } */
                } else {
                    n = 0;
                }
            } else {
                n = var_name_len((char *)p + 1);
                if (n > 0) {
                    name_start = p + 1;
                    consumed = n + 1; /* $ + name */
                }
            }

            if (n == 0) {
                /* Lone '$' or invalid reference: keep it literally */
                buffer[out++] = *p++;
                continue;
            }

            char name[1024];
            if (n >= sizeof(name)) {
                n = sizeof(name) - 1;
            }
            memcpy(name, name_start, n);
            name[n] = '\0';

            char *value = lookup_shell_variable(name);
            if (value != NULL) {
                size_t vlen = strlen(value);
                if (vlen > MAX_ARG_LEN - 1 - out) {
                    vlen = MAX_ARG_LEN - 1 - out;
                }
                memcpy(buffer + out, value, vlen);
                out += vlen;
            }
            /* unset variable: append nothing */

            p += consumed;
        }
        buffer[out] = '\0';

        if (buffer[0] == '\0') {
            free(buffer); // expanded to nothing: drop the word entirely
            continue;
        }
        argv[kept++] = buffer;
    }

    argv[kept] = NULL;
    return kept;
}

char *lookup_shell_variable(char *name) {
    for (int i = 0; i < VariableCount; i++) {
        if (strcmp(Variables[i].name, name) == 0) {
            return Variables[i].value;
        }
    }
    return NULL;
}

void free_expanded_variables(char *argv[], int argc, char *inputs,
                             size_t input_len) {
    for (int i = 0; i < argc; i++) {
        if (argv[i] < inputs || argv[i] > inputs + input_len) {
            free(argv[i]);
        }
    }
}

void do_declare(char *argv[]) {
    if (argv[1] == NULL) {
        return;
    }

    if (strcmp(argv[1], "-p") == 0) {
        if (argv[2] != NULL) {
            char *value = lookup_shell_variable(argv[2]);
            if (value != NULL) {
                printf("declare -- %s=\"%s\"\n", argv[2], value);
            } else {
                printf("declare: %s: not found\n", argv[2]);
            }
        }
    } else {
        char *name = strtok(argv[1], "=");
        char *value = strtok(NULL, "=");

        if (name && value) {
            int name_len = strlen(name);

            for (int i = 0; i < name_len; i++) {
                if (isdigit(name[0]) || !(isalnum(name[i]) || name[i] == '_')) {
                    fprintf(stderr,
                            "declare: `%s=%s': not a valid identifier\n", name,
                            value);
                    return;
                }
            }

            for (int i = 0; i < VariableCount; i++) {
                if (strcmp(Variables[i].name, name) == 0) {
                    strcpy(Variables[i].value, value);
                    return;
                }
            }
            strcpy(Variables[VariableCount].value, value);
            strcpy(Variables[VariableCount].name, name);
            VariableCount++;
        }
    }
}
