
```markdown
# WSH - A Simple Shell Implementation

## Overview

WSH (short for Whatever Shell) is a simple implementation of a Unix shell in C. It demonstrates the basics of how a shell works. That is, it reads commands from the user and executes them. The purpose of this project is to understand the inner workings of shells by implementing built-in commands, signal handling, process management, and input/output redirection.

## Features

- Execution of external commands.
- Supports job control, including foreground and background jobs with `fg` and `bg` commands.
- Built-in commands: `cd`, `exit`, `jobs`, `fg`, `bg`.
- Signal handling for `SIGINT` (Ctrl+C), `SIGTSTP` (Ctrl+Z), and `SIGCHLD`.
- Basic pipeline support (command1 | command2).
- Background execution of commands with `&`.
- Error handling for various scenarios.

## How to Build and Run

Ensure you have GCC installed on your system. You can compile and run WSH using the included Makefile.

### Compiling

To compile the shell, use the `make` command in the directory containing the source code and Makefile:

```bash
make
```

This command creates the executable named `wsh`.

### Running the Shell

To start the shell in interactive mode, simply run:

```bash
./wsh
```

To run the shell with a script, you can redirect input from a file:

```bash
./wsh < script.txt
```

## Usage Examples

- Running a command in the foreground:

  ```bash
  wsh> ls -l
  ```

- Running a command in the background:

  ```bash
  wsh> sleep 10 &
  ```

- Listing jobs:

  ```bash
  wsh> jobs
  ```

- Bringing a job to the foreground:

  ```bash
  wsh> fg %1
  ```

- Sending a job to the background:

  ```bash
  wsh> bg %1
  ```

