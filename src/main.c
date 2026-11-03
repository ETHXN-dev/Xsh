#include "builtins.h"
#include "executor.h"
#include "redirect.h"
#include "tokenize.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define IS_DOT_OR_DOTDOT(s)                                                    \
    (s[0] == '.' && ((s[1] == '\0') || (s[1] == '.' && s[2] == '\0')))

char *command_generator(const char *text, int state);
char *completer_generator(const char *text, int state);
char **run_completer(const char *completer_path, const char *command,
                     const char *partial_word, const char *prev_word);
char **my_completion(const char *text, int start, int end);

extern char **environ;

/* Global to hold the last run_completer results */
char **completer_results;

int main(void) {
    // Flush after every printf
    setbuf(stdout, NULL);

    rl_attempted_completion_function = my_completion;

    const char *histfile = getenv("HISTFILE");
    if (histfile != NULL) {
        read_history(histfile);
        Persisted_history = history_length;
    }

    while (1) {
        check_jobs(false);

        char *inputs = readline("$ ");
        if (inputs == NULL) {
            if (histfile) {
                write_history(histfile);
            }
            exit(EXIT_SUCCESS);
        }
        add_history(inputs);

        char *command_run = strdup(inputs);
        if (command_run == NULL) {
            free(inputs);
            perror("strdup");
            exit(EXIT_FAILURE);
        }

        char *arguments[MAX_ARGS];

        int arg_count = tokenize(arguments, inputs);
        if (arg_count == 0) {
            free(inputs);
            free(command_run);
            continue; // blank line, nothing to do
        }
        if (arg_count < 0) {
            print_tokenize_error(arg_count);
            free(inputs);
            free(command_run);
            continue;
        }

        size_t input_len = strlen(command_run);
        arg_count = expand_variables(arguments, arg_count);
        if (arg_count == 0) { // every word expanded to nothing
            free(inputs);
            free(command_run);
            continue;
        }

        bool has_pipe = false;
        for (int i = 1; i < arg_count; i++) {
            if (strcmp(arguments[i], "|") == 0) {
                has_pipe = true;
                break;
            }
        }

        if (has_pipe) {
            if (valid_pipeline(arguments)) {
                run_pipeline(arguments);
            }
            free_expanded_variables(arguments, arg_count, inputs, input_len);
            free(inputs);
            free(command_run);
            continue;
        }

        /* Handle background jobs */
        if (strcmp(arguments[arg_count - 1], "&") == 0) {
            Highest_job_number++, Running_jobs_count++;
            Running_jobs[Highest_job_number].job_number = Highest_job_number;
            Running_jobs[Highest_job_number].running_status = true;

            strncpy(Running_jobs[Highest_job_number].command_string,
                    command_run, (MAX_CMD_LEN * MAX_ARGS));
            Running_jobs[Highest_job_number]
                .command_string[(MAX_CMD_LEN * MAX_ARGS) - 1] = '\0';
            free(command_run);

            arguments[arg_count - 1] = NULL;
            run_external_program(arguments, true);

            free_expanded_variables(arguments, arg_count, inputs, input_len);
            free(inputs);
            continue;
        }

        RedirectType *redirect = NULL;
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
            free_expanded_variables(arguments, arg_count, inputs, input_len);
            free(inputs);
            continue;
        }

        if (redirect != NULL) {
            if (filename == NULL) {
                // user didn't enter a file to redirect to
                fprintf(stderr,
                        "syntax error near unexpected token `newline'\n");
                free_expanded_variables(arguments, arg_count, inputs,
                                        input_len);
                free(inputs);
                continue;
            } else {
                redirect_stream(arguments, filename, redirect->fd,
                                redirect->append);
            }
        } else {
            execute_command(arguments);
        }

        free_expanded_variables(arguments, arg_count, inputs, input_len);

        free(inputs);
        free(command_run);
    }

    return 0;
}

/* Completion generator for GNU readline's rl_completion_matches().
 * Readline calls this repeatedly with the same partial `text`,
 * incrementing `state` each time; state == 0 signals a new completion
 * attempt, so we reset our position in `builtins`. Returns a strdup'd name
 * of the next builtin whose name starts with `text`, or NULL when no more
 * matches remain. */
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

    while (Builtins[index].name != NULL) {
        char *name = Builtins[index].name;
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

char *completer_generator(const char *text, int state) {
    static int cached_count;
    if (state == 0) {
        cached_count = 0;
        while (completer_results[cached_count])
            cached_count++;
    }
    if (state < cached_count) {
        return completer_results[state];
    }
    return NULL;
}

/*
 * Invoke a program completer and return its stdout as a list of lines.
 *
 * @param completer_path  path to the completer binary (e.g.
 * "/usr/bin/my_completer")
 * @param command         argv[1] — the command being completed (e.g. "git")
 * @param partial_word    argv[2] — the word at the cursor (e.g. "ch")
 * @param prev_word       argv[3] — word before the partial (e.g. "" if
 * none)
 * @return malloc'd array of strings (NULL-terminated), or NULL on failure.
 */
char **run_completer(const char *completer_path, const char *command,
                     const char *partial_word, const char *prev_word) {
    int pipefd[2]; // pipefd[0] refers to the read end of the pipe.
                   // pipefd[1] refers to the write end of the pipe.
    if (pipe(pipefd) == -1) {
        return NULL;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        /* ---- Child ---- */
        close(pipefd[0]);               // close read end
        dup2(pipefd[1], STDOUT_FILENO); // stdout -> pipe
        close(pipefd[1]);               // close original write end

        /* Build argv */
        char *argv[] = {(char *)completer_path, (char *)command,
                        (char *)partial_word, (char *)prev_word, NULL};

        execve(completer_path, argv, environ);
        _exit(127); // execv failed
    }

    /* ---- Parent ---- */
    close(pipefd[1]); // close write end so read() can see EOF

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        return NULL;
    }

    /* Starts as NULL/0 so getline allocates on first call */
    char *line = NULL;
    size_t line_cap = 0;

    char **result = NULL;
    size_t count = 0, cap = 0;
    ssize_t n;

    while ((n = getline(&line, &line_cap, fp)) != -1) {
        /* Strip trailing newline */
        if (n > 0 && line[n - 1] == '\n') {
            line[n - 1] = '\0';
        }

        /* Skip empty lines */
        if (n == 1) {
            free(line);
            line = NULL;
            line_cap = 0;
            continue;
        }

        /* Grow the result array */
        if (count == cap) {
            cap = cap ? cap * 2 : 8;
            result = realloc(result, sizeof(char *) * (cap + 1));
        }

        /* Take ownership of getline's buffer for this line */
        result[count++] = line;
        line = NULL; /* force getline to allocate fresh next iteration */
        line_cap = 0;
    }

    free(line);
    fclose(fp); /* closes pipefd[0] too */

    int status;
    waitpid(pid, &status, 0);

    if (result) {
        result[count] = NULL;
    }

    return result;
}

/* Custom completion function for GNU readline (set via
 * rl_attempted_completion_function). Called once per completion attempt
 * with the full line context: `text` is the word being completed,
 * `start`/`end` are its offsets into rl_line_buffer. We only offer
 * builtin-name completions when the word being completed is the first word
 * on the line (start == 0); otherwise we return NULL so readline falls back
 * to its default filename completion. */
char **my_completion(const char *text, int start, int end) {
    if (start == 0) {
        return rl_completion_matches(text, command_generator);
    } else {
        /* Extract first word (the command) */
        char first_word[MAX_CMD_LEN] = "";
        for (int i = 0; rl_line_buffer[i] && rl_line_buffer[i] != ' ' &&
                        i < MAX_CMD_LEN - 1;
             i++) {
            first_word[i] = rl_line_buffer[i];
        }

        /* Previous word: skip spaces leftward, then walk the word */
        char prev_word[MAX_CMD_LEN] = "";
        int j = start - 1;
        while (j > 0 && rl_line_buffer[j] == ' ') {
            j--;
        }
        int end = j;
        while (j > 0 && rl_line_buffer[j - 1] != ' ') {
            j--;
        }
        int len = end - j + 1;
        if (len > 0) {
            if (len >= MAX_CMD_LEN) {
                len = MAX_CMD_LEN - 1;
            }
            memcpy(prev_word, rl_line_buffer + j, len);
            prev_word[len] = '\0';
        }

        /* Look up and invoke */
        for (int i = 0; Completions_registered[i].command[0] != '\0'; i++) {
            if (strcmp(Completions_registered[i].command, first_word) == 0) {
                rl_attempted_completion_over = 1;

                char comp_point[1024];
                if ((snprintf(comp_point, sizeof(comp_point), "%d", rl_point)) <
                    0) {
                    return NULL;
                }
                if ((setenv("COMP_POINT", comp_point, 1) == -1) ||
                    (setenv("COMP_LINE", rl_line_buffer, 1) == -1)) {
                    return NULL;
                }

                completer_results =
                    run_completer(Completions_registered[i].script_path,
                                  first_word, text, prev_word);
                if (completer_results) {
                    char **result =
                        rl_completion_matches(text, completer_generator);
                    free(completer_results); /* free the array of pointers,
                                             NOT the strings */
                    return result;
                }

                break;
            }
        }
    }

    return NULL;
}
