#ifndef TOKENIZE_H
#define TOKENIZE_H

#define MAX_ARGS 256

typedef enum {
    SEEKING,
    IN_ARG,
    IN_SINGLE_QUOTE,
    IN_DOUBLE_QUOTE,
    ESCAPING
} TokenizerState;

typedef enum {
    TOKENIZE_ERR_TOO_MANY_ARGS = -1,
    TOKENIZE_ERR_UNTERMINATED_QUOTE = -2,
    TOKENIZE_ERR_TRAILING_ESCAPE = -3,
} TokenizeErr;

int tokenize(char *args[], char *buf);
void print_tokenize_error(int err);

#endif
