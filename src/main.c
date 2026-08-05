#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int get_input(char *args[]);
void free_inputs(char *args[]);

int main(int argc, char *argv[]) {
    // Flush after every printf
    setbuf(stdout, NULL);

    while (1) {
        printf("$ ");

        char *argv[10];
        argc = get_input(argv);

        if (argc < 0) {
            putchar('\n');
            break; // Any negative return code means stop execution
        }

        if (strcmp(argv[0], "exit") == 0) {
            free_inputs(argv);
            break;
        } else if (strcmp(argv[0], "echo") == 0) {
            /* print the arguments entered after echo */
            for (int i = 1; i < argc; i++)
                printf("%s ", argv[i]);
            putchar('\n');
        } else {
            printf("%s: command not found\n", argv[0]);
        }

        free_inputs(argv);
    }

    return 0;
}

int get_input(char *args[]) {
    char command[1024];
    if (fgets(command, sizeof(command), stdin) == NULL) {
        if (feof(stdin)) {
            /* Signal EOF to caller (e.g., user pressed Ctrl+D) */
            return -1;
        } else if (ferror(stdin)) {
            perror("fgets failed");
            return -1;
        }
    }

    /* remove the trailing newline */
    command[strcspn(command, "\n")] = '\0';

    /* tokenize the input to become arguments */
    int argc = 0;
    char *token = strtok(command, " ");
    while (token != NULL) {
        args[argc] = strdup(token);
        /* Error handling for NULL return */
        if (args[argc] == NULL) {
            perror("strdup failed");
            exit(EXIT_FAILURE);
        }
        token = strtok(NULL, " ");
        argc++;
    }

    args[argc] = NULL;

    return argc;
}

void free_inputs(char *args[]) {
    while (*args != NULL) {
        free(*args);
        args++;
    }
}
