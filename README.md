**Quash: Build Your Own Shell**

Author: Bijay Pandey
Course: Operating Systems Lab
Environment: Codio (Ubuntu GNU/Linux)
Language: C (compiled with GCC using -std=c99)

**Objective**

The goal of this project is to build a working UNIX-like command-line shell named Quash.
This shell demonstrates how real shells work internally — handling user commands, process creation, signals, and I/O redirection.
The project provides hands-on experience with key UNIX system calls like fork(), execvp(), waitpid(), signal(), chdir(), and setenv().

**Features Implemented**

**1. Shell Prompt**

Displays the current working directory before each command.

Implemented using getcwd().

**2. Built-in Commands**

cd: Change the current working directory

pwd: Print the current working directory

echo: Print a message and resolve $VARIABLES

env: Display environment variables

setenv: Set an environment variable

exit: Terminate the shell

Example:

/home/codio/workspace> echo $HOME
/home/codio

**3. Process Execution (fork + execvp)**

For non-built-in commands, the shell uses fork() to create a child process and execvp() to execute the command.

The parent process waits for the child using waitpid().

/home/codio/workspace> ls
Makefile  shell.c  quash

**4. Background Processes (&)**

When a command ends with &, it runs in the background.

The parent does not wait for it to finish.

/home/codio/workspace> sleep 5 &
[bg] started pid 1234
/home/codio/workspace> echo done
done

**5. Signal Handling (Ctrl-C)**

A custom SIGINT handler prevents the shell from quitting when Ctrl-C is pressed.

Instead, it interrupts the current process and brings back the prompt.

/home/codio/workspace> sleep 20
^C
/home/codio/workspace>

**6. Process Timeout (10 seconds)**

Any foreground process exceeding 10 seconds is automatically killed.

/home/codio/workspace> sleep 15
Process exceeded 10s and was terminated.

**7. I/O Redirection and Piping**

Output redirection: >

Input redirection: <

Single pipe: |

Examples:

cat shell.c > output.txt
cat < output.txt
cat shell.c | grep execvp

**Testing Summary**

| Feature     | Command Example       | Result              | Status |   |
| ----------- | --------------------- | ------------------- | ------ | - |
| Prompt      | —                     | shows current dir   | ✅      |   |
| cd          | cd /                  | works               | ✅      |   |
| pwd         | pwd                   | prints path         | ✅      |   |
| echo        | echo $HOME            | expands variable    | ✅      |   |
| env         | env PATH              | prints PATH         | ✅      |   |
| setenv      | setenv greeting hello | sets variable       | ✅      |   |
| Fork/Exec   | ls                    | runs system command | ✅      |   |
| Background  | sleep 5 &             | runs in bg          | ✅      |   |
| Timeout     | sleep 15              | killed after 10s    | ✅      |   |
| Ctrl-C      | sleep 20 + Ctrl-C     | returns prompt      | ✅      |   |
| Redirection | echo hi > file.txt    | works               | ✅      |   |
| Piping      | cat shell.c           | grep main           | works  | ✅ |

**File Structure**

Lab-3-Quash/
├── shell.c — main source code
├── Makefile — build script
└── README.md — project documentation

**Compilation and Execution**

make clean
make
./quash

**Design Summary**

This project integrates:

Process creation and control with fork() and execvp()

Signal handling with SIGINT and SIGALRM

Environment variable management

Background job control

I/O redirection and simple piping

Process timeout logic using alarm() and kill()

All components were tested in Codio and verified against the example shell (ref_shell).

**Reflection**

Building Quash provided practical insight into how shells like Bash handle user input, process creation, and asynchronous events.
Implementing signals, process timers, and redirection demonstrated how complex system behaviors are managed through simple system calls.
This project reinforced concepts of process hierarchy, environment scope, and preemptive execution.
