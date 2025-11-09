#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <limits.h>

#define MAX_TOKENS 128
#define LINE_BUFSZ 4096

extern char **environ;

static volatile sig_atomic_t got_sigint = 0;
static volatile sig_atomic_t got_alarm = 0;
static pid_t fg_child = -1;

static void print_prompt(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("%s> ", cwd);
    } else {
        printf("> ");
    }
    fflush(stdout);
}

static char *dupstr(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *expand_env(const char *tok) {
    if (tok[0] != '$' || tok[1] == '\0') return dupstr(tok);
    const char *name = tok + 1;
    const char *val = getenv(name);
    return dupstr(val ? val : "");
}

static int tokenize(char *line, char *argv[], int max_tokens) {
    int argc = 0;
    for (char *t = strtok(line, " \t\r\n"); t && argc < max_tokens - 1; t = strtok(NULL, " \t\r\n")) {
        char *expanded = (t[0] == '$') ? expand_env(t) : dupstr(t);
        argv[argc++] = expanded;
    }
    argv[argc] = NULL;
    return argc;
}

static void free_argv(char *argv[]) {
    for (int i = 0; argv[i]; i++) free(argv[i]);
}

static int bi_cd(char *argv[]) {
    const char *target = argv[1] ? argv[1] : getenv("HOME");
    if (!target) { fprintf(stderr, "cd: no target\n"); return 1; }
    if (chdir(target) != 0) { perror("cd"); return 1; }
    return 0;
}

static int bi_pwd(void) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) { puts(cwd); return 0; }
    perror("pwd"); return 1;
}

static int bi_echo(char *argv[]) {
    for (int i = 1; argv[i]; i++) {
        if (i > 1) putchar(' ');
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return 0;
}

static int bi_env(char *argv[]) {
    if (!argv[1]) {
        for (char **e = environ; *e; ++e) puts(*e);
    } else {
        for (int i = 1; argv[i]; i++) {
            const char *v = getenv(argv[i]);
            if (v) puts(v);
        }
    }
    return 0;
}

static int bi_setenv(char *argv[]) {
    if (!argv[1]) { fprintf(stderr, "setenv NAME=VALUE or setenv NAME VALUE\n"); return 1; }
    const char *name = NULL, *val = NULL;
    char *eq = strchr(argv[1], '=');
    if (eq) {
        *eq = '\0';
        name = argv[1];
        val = eq + 1;
    } else {
        if (!argv[2]) { fprintf(stderr, "setenv NAME VALUE\n"); return 1; }
        name = argv[1];
        val = argv[2];
    }
    if (setenv(name, val, 1) != 0) { perror("setenv"); return 1; }
    return 0;
}

static bool is_builtin(const char *cmd) {
    return cmd && (!strcmp(cmd, "cd") || !strcmp(cmd, "pwd") || !strcmp(cmd, "echo") || !strcmp(cmd, "env") || !strcmp(cmd, "setenv") || !strcmp(cmd, "exit"));
}

static int run_builtin(char *argv[]) {
    const char *cmd = argv[0];
    if (!strcmp(cmd, "cd")) return bi_cd(argv);
    if (!strcmp(cmd, "pwd")) return bi_pwd();
    if (!strcmp(cmd, "echo")) return bi_echo(argv);
    if (!strcmp(cmd, "env")) return bi_env(argv);
    if (!strcmp(cmd, "setenv")) return bi_setenv(argv);
    if (!strcmp(cmd, "exit")) exit(0);
    return 1;
}

static void on_sigint(int sig) {
    (void)sig;
    got_sigint = 1;
    (void)write(STDOUT_FILENO, "\n", 1);
}
static void on_sigalrm(int sig) {
    (void)sig;
    got_alarm = 1;
    if (fg_child > 0) {
        kill(fg_child, SIGKILL);
    }
}

struct redir {
    char *in_path;
    char *out_path;
};

static void apply_redir(struct redir *rd) {
    if (rd->in_path) {
        int fd = open(rd->in_path, O_RDONLY);
        if (fd < 0) { perror("open <"); _exit(127); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    if (rd->out_path) {
        int fd = open(rd->out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) { perror("open >"); _exit(127); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

static int split_on_token(char *argv[], const char *tok, char *left[], char *right[]) {
    int i = 0, li = 0, ri = 0, cut = -1;
    for (; argv[i]; i++) {
        if (!strcmp(argv[i], tok)) { cut = i; break; }
        left[li++] = argv[i];
    }
    left[li] = NULL;
    if (cut == -1) { right[0] = NULL; return 0; }
    for (i = cut + 1; argv[i]; i++) right[ri++] = argv[i];
    right[ri] = NULL;
    return 1;
}

static int extract_redirs(char *argv[], struct redir *rd) {
    rd->in_path = rd->out_path = NULL;
    int w = 0;
    for (int i = 0; argv[i]; i++) {
        if (!strcmp(argv[i], "<") || !strcmp(argv[i], ">")) {
            int is_out = (argv[i][0] == '>');
            if (!argv[i + 1]) { fprintf(stderr, "syntax error near `%s`\n", argv[i]); return -1; }
            if (is_out) rd->out_path = argv[i + 1];
            else rd->in_path = argv[i + 1];
            i++;
            continue;
        }
        argv[w++] = argv[i];
    }
    argv[w] = NULL;
    return 0;
}

static void run_external(char *argv[], bool background) {
    char *left[MAX_TOKENS], *right[MAX_TOKENS];
    int has_pipe = split_on_token(argv, "|", left, right);

    if (has_pipe) {
        int pipefd[2];
        if (pipe(pipefd) < 0) { perror("pipe"); return; }

        pid_t a = fork();
        if (a < 0) { perror("fork"); close(pipefd[0]); close(pipefd[1]); return; }
        if (a == 0) {
            signal(SIGINT, SIG_DFL);
            struct redir rd = {0};
            if (extract_redirs(left, &rd) < 0) _exit(127);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]); close(pipefd[1]);
            apply_redir(&rd);
            execvp(left[0], left);
            perror("execvp"); _exit(127);
        }

        pid_t b = fork();
        if (b < 0) { perror("fork"); close(pipefd[0]); close(pipefd[1]); return; }
        if (b == 0) {
            signal(SIGINT, SIG_DFL);
            struct redir rd = {0};
            if (extract_redirs(right, &rd) < 0) _exit(127);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[1]); close(pipefd[0]);
            apply_redir(&rd);
            execvp(right[0], right);
            perror("execvp"); _exit(127);
        }

        close(pipefd[0]); close(pipefd[1]);
        int st;
        waitpid(a, &st, 0);
        waitpid(b, &st, 0);
        return;
    }

    struct redir rd = {0};
    if (extract_redirs(argv, &rd) < 0) return;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return; }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        apply_redir(&rd);
        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    }

    if (background) {
        printf("[bg] started pid %d\n", pid);
        return;
    }

    fg_child = pid;
    got_alarm = 0;
    alarm(10);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
    }
    alarm(0);
    fg_child = -1;
    if (got_alarm) {
        fprintf(stderr, "Process exceeded 10s and was terminated.\n");
    }
}

int main(void) {
    struct sigaction sa_int; memset(&sa_int, 0, sizeof sa_int);
    struct sigaction sa_alrm; memset(&sa_alrm, 0, sizeof sa_alrm);
    sa_int.sa_handler = on_sigint; sigemptyset(&sa_int.sa_mask); sa_int.sa_flags = 0;
    sa_alrm.sa_handler = on_sigalrm; sigemptyset(&sa_alrm.sa_mask); sa_alrm.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGALRM, &sa_alrm, NULL);

    char line[LINE_BUFSZ];
    while (1) {
        got_sigint = 0;
        print_prompt();

        if (!fgets(line, sizeof(line), stdin)) {
            puts("");
            break;
        }

        char *argv[MAX_TOKENS] = {0};
        int argc = tokenize(line, argv, MAX_TOKENS);
        if (argc == 0) { continue; }

        bool background = false;
        if (argc > 0 && !strcmp(argv[argc - 1], "&")) {
            background = true;
            free(argv[argc - 1]);
            argv[--argc] = NULL;
        }

        if (is_builtin(argv[0])) {
            run_builtin(argv);
            free_argv(argv);
            continue;
        }

        run_external(argv, background);
        free_argv(argv);
    }
    return 0;
}
