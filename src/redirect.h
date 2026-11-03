#ifndef REDIRECT_H
#define REDIRECT_H

#include <stdbool.h>

typedef struct {
    char *name;
    int fd;
    bool append;
} RedirectType;

RedirectType *classify_redirect(const char *s);
void redirect_stream(char *argv[], char *filename, int stream, int append);

#endif
