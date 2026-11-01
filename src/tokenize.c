#include "tokenize.h"
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>

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

    TokenizerState state = SEEKING;

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
