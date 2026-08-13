[![progress-banner](https://backend.codecrafters.io/progress/shell/99df4373-61a8-4e15-9f90-322a8349ed01)](https://app.codecrafters.io/users/Ethxxnvl?r=2qF)

# Build Your Own Shell — C

A POSIX-style shell written from scratch in C, built stage by stage as part of
CodeCrafters' ["Build Your Own Shell"](https://app.codecrafters.io/courses/shell/overview)
challenge. It runs a read-eval-print loop, supports a handful of builtins, and
falls back to launching external programs found on `PATH`.

## What it does

```
$ pwd
/home/user/project
$ cd ..
$ echo hello world
hello world
$ type cd
cd is a shell builtin
$ type ls
ls is /usr/bin/ls
$ ls -la
... (runs the real ls binary)
$ exit
```

Builtins: `exit`, `echo`, `type`, `pwd`, `cd` (including `cd ~`). Anything else
is looked up on `PATH` and run via `fork()` + `execve()`.

## Building & running

```sh
# Requires cmake
./your_program.sh
```

## How it was built

CodeCrafters works by giving you one small stage at a time and having you
submit against their test suite before moving to the next. That's the shape
this project grew in — small, working increments rather than one big design
up front. Below is the actual progression, written from the diffs, including
the points where I stopped to refactor before adding new behavior.

### Stage 0 — starting point

The provided skeleton: a `main()` that flushes stdout and has a commented-out
`printf("$ ")`. Nothing runs yet — it's just there to prove the build/test
pipeline works.

### Stage 1 — print a prompt

Uncommented the prompt. The shell now prints `$ ` once and exits. Trivial, but
it's the first thing CodeCrafters' test harness checks for, since every later
stage assumes the prompt is there.

### Stage 2 — read a command

Added `fgets` to read a line from stdin, stripped the trailing newline with
`strcspn`, and printed `<command>: command not found`. At this point the shell
still only handles one command and then exits — no loop yet.

### Stage 3 — turn it into a loop (the REPL)

Wrapped the read/print logic in `while (1)`. This is the point where it
actually became a *shell* instead of a one-shot program — read a line,
respond, read again, forever, until something tells it to stop.

### Stage 4 — the `exit` builtin

Added a check for the literal string `"exit"` that breaks out of the loop.
This is the first "builtin" — a command handled directly by the shell instead
of being looked up as an external program.

### Stage 5 — argument parsing and `echo`

This is the biggest jump so far. Instead of treating the whole line as one
command, I added `get_input()`, which tokenizes the line on spaces into an
`argv[]`-style array using `strtok`, and returns an argument count. That let
me:
- distinguish the command (`argv[0]`) from its arguments
- implement `echo`, which just prints back everything after it

It also introduced a negative-return-code convention for EOF/errors (e.g.
Ctrl+D), which the main loop checks for and uses to exit cleanly.

### Refactor — splitting responsibilities

Before adding the next builtin, I split parsing out of the single big
`get_input()` and introduced `MAX_ARGS` as an explicit bound instead of a
magic number. No new behavior here — it was purely about making room for
`type` and `PATH` lookup without the input-handling function doing too much.

### Stage 6 — `type` and `PATH` lookup

Added `get_path()`, which splits the `PATH` environment variable on `:` and
checks each directory for an executable file matching the command name
(`access(..., X_OK)`). `type` uses this to report whether a command is a
shell builtin or resolves to a real path on disk (or "not found").

This is also where the shell starts distinguishing *builtin* commands from
*external* commands as a concept, not just as a runtime check — that
distinction drives everything from here on.

### Stage 7 — actually running external programs

`type` could locate a program, but the shell couldn't run one yet. Added
`run_external_program()`, which:
1. resolves the command via `get_path()`
2. `fork()`s a child process
3. in the child, calls `execv()` to replace the child with the target program
4. in the parent, `waitpid()`s for the child to finish before printing the
   next prompt

This is the standard Unix fork/exec/wait pattern and is what turns the shell
from "toy REPL" into something that can run `ls`, `cat`, `grep`, etc.

### Stage 8 — `pwd`

Added a `pwd` builtin using `getcwd()`, with `free()` on the returned buffer
and a `perror()` fallback if it fails.

### Stage 9 — `cd`

Added a `cd` builtin using `chdir()`, handling both absolute and relative
paths, with an error message via `strerror(errno)` when the target doesn't
exist or isn't accessible.

### Stage 10 — `cd ~`

Extended `cd` to special-case `~` (and no argument) by reading `$HOME` from
the environment and `chdir`-ing there, matching how a real shell behaves when
you type `cd` with nothing after it.

### Refactor — cleaner `cd` error handling

Reworked the `cd` error paths so failures from `chdir(home)` are reported the
same way as failures from `chdir(target)`, instead of silently succeeding.
No new features — just making the existing behavior actually correct.

### Refactor — splitting input handling again

The original `get_input()` mixed two concerns: reading a raw line from stdin,
and tokenizing that line into arguments. Split it into `read_line()` and
`tokenize()`, each doing one job. This makes the code easier to extend later
(e.g. if quoting or piping ever get added, only `tokenize()` needs to change).

## A note on the commit history

CodeCrafters' CLI stamps every push with the same `codecrafters submit [skip ci]`
message, so the git log itself doesn't say what changed at each step — this
README is the readable version of that history. If you want to see the actual
code for any stage, `git log --oneline` gives you the hashes in order, and
`git show <hash> -- src/main.c` shows exactly what that commit changed.
