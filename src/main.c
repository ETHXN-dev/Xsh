#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 10

int get_input(char *args[], char *buf, size_t buf_size);
char *get_path(char *command);

int main(void) {
    // Flush after every printf
    setbuf(stdout, NULL);

    char inputs[1024];

    while (1) {
        printf("$ ");

        char *tokens[MAX_ARGS];
        int token_count = get_input(tokens, inputs, sizeof(inputs));

        if (token_count < 0) {
            putchar('\n');
            break; // Any negative return code means stop execution
        } else if (token_count == 0) {
            continue;
        }

        if (strcmp(tokens[0], "exit") == 0) {
            break;
        } else if (strcmp(tokens[0], "echo") == 0) {
            /* print the arguments entered after echo */
            for (int i = 1; i < token_count; i++)
                printf("%s ", tokens[i]);
            putchar('\n');
        } else if (strcmp(tokens[0], "type") == 0) {
            if (!tokens[1])
                continue;

            if (strcmp(tokens[1], "type") == 0 ||
                strcmp(tokens[1], "exit") == 0 ||
                strcmp(tokens[1], "echo") == 0) {
                printf("%s is a shell builtin\n", tokens[1]);
                continue;
            } else {
                char *full_path = get_path(tokens[1]);

                if (full_path) {
                    printf("%s is %s\n", tokens[1], full_path);
                    free(full_path);
                } else {
                    printf("%s: not found\n", tokens[1]);
                }
            }

        } else {
            printf("%s: command not found\n", tokens[0]);
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
    while (token != NULL) {
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
