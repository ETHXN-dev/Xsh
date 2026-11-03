#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stdbool.h>

void run_external_program(char *argv[], bool background);
void execute_command(char *argv[]);
bool valid_pipeline(char *argv[]);
void run_pipeline(char *argv[]);
void run_in_child(char *argv[]);

#endif
