#ifndef BUILTINS_H
#define BUILTINS_H

#include <fcntl.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 256
#define MAX_COMPLETIONS 1024
#define MAX_ARG_LEN 4096
#define MAX_JOBS 1024
#define MAX_SHELL_VARIABLES 1024

typedef void (*builtin_func)(char *argv[]);

typedef struct {
    char *name;
    builtin_func func;
} Builtin;

typedef struct {
    char command[MAX_CMD_LEN];
    char script_path[PATH_MAX];
} Completion;

typedef struct {
    int job_number;
    pid_t pid;
    char command_string[MAX_CMD_LEN * MAX_ARGS];
    bool running_status;
} Job;

typedef struct {
    char name[1024];
    char value[1024];
} ShellVariable;

extern Builtin Builtins[];

extern Completion Completions_registered[];

extern int Highest_job_number;
extern int Running_jobs_count;
extern Job Running_jobs[];

extern int Persisted_history;

void do_exit(char *argv[]);
void do_echo(char *argv[]);
char *get_path(char *command);
void do_type(char *argv[]);
void do_pwd(char *argv[]);
void do_cd(char *argv[]);
void do_complete(char *argv[]);
void do_jobs(char *argv[]);
void check_jobs(bool display_all);
void do_history(char *argv[]);
void do_declare(char *argv[]);
char *lookup_shell_variable(char *name);
size_t var_name_len(char *s);
bool has_var_ref(char *s);
int expand_variables(char *argv[], int argc);
void free_expanded_variables(char *argv[], int argc, char *inputs,
                             size_t input_len);

#endif
