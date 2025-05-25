#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100

char* history[MAX_HISTORY];
int history_count = 0;
volatile sig_atomic_t shell_running = 1;

// Ctrl+C handler: only interrupt child, not shell
void handle_sigint(int sig) {
    write(STDOUT_FILENO, "\nsh> ", 5);
}

// Display shell prompt
void display_prompt() {
    printf("sh> ");
    fflush(stdout);
}

// Save command to history
void add_to_history(char* cmd) {
    if (history_count < MAX_HISTORY) {
        history[history_count++] = strdup(cmd);
    }
}

// Tokenize a command string into args
void parse_args(char* cmd, char** args) {
    int i = 0;
    char* token = strtok(cmd, " \t\n");
    while (token && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;
}

// Execute one command with optional I/O redirection
int execute_simple(char* cmd) {
    char* args[MAX_ARGS];
    char* input_file = NULL;
    char* output_file = NULL;
    int append = 0;

    char* token = strtok(cmd, " \t\n");
    int i = 0;
    while (token && i < MAX_ARGS - 1) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " \t\n");
            input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " \t\n");
            output_file = token;
            append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, " \t\n");
            output_file = token;
            append = 1;
        } else {
            args[i++] = token;
        }
        token = strtok(NULL, " \t\n");
    }
    args[i] = NULL;

    if (args[0] == NULL) return 0;
    if (strcmp(args[0], "exit") == 0) exit(0);
    if (strcmp(args[0], "history") == 0) {
        for (int j = 0; j < history_count; ++j)
            printf("%d: %s", j + 1, history[j]);
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child
        if (input_file) {
            int fd = open(input_file, O_RDONLY);
            if (fd < 0) { perror("input redirection"); exit(1); }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        if (output_file) {
            int fd = open(output_file, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
            if (fd < 0) { perror("output redirection"); exit(1); }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        execvp(args[0], args);
        perror("execvp failed");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
}

// Handle piped commands
int execute_piped(char* cmdline) {
    char* cmds[MAX_ARGS];
    int num_cmds = 0;
    char* tok = strtok(cmdline, "|");
    while (tok && num_cmds < MAX_ARGS - 1) {
        cmds[num_cmds++] = tok;
        tok = strtok(NULL, "|");
    }

    int prev_fd = -1;
    int status = 0;

    for (int i = 0; i < num_cmds; i++) {
        int pipe_fd[2];
        if (i < num_cmds - 1) pipe(pipe_fd);

        pid_t pid = fork();
        if (pid == 0) {
            // Child
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if (i < num_cmds - 1) {
                dup2(pipe_fd[1], STDOUT_FILENO);
                close(pipe_fd[0]);
                close(pipe_fd[1]);
            }

            // Redirection in each pipe segment
            execute_simple(cmds[i]);
            exit(0);
        } else {
            // Parent
            waitpid(pid, &status, 0);
            if (prev_fd != -1) close(prev_fd);
            if (i < num_cmds - 1) {
                close(pipe_fd[1]);
                prev_fd = pipe_fd[0];
            }
        }
    }

    return status;
}

// Split by ; and &&
void process_input(char* line) {
    char* saveptr;
    char* token = strtok_r(line, ";", &saveptr);

    while (token) {
        char* subptr;
        char* and_token = strtok_r(token, "&&", &subptr);
        int run_next = 1;
        while (and_token) {
            if (run_next) {
                if (strchr(and_token, '|')) {
                    run_next = (execute_piped(and_token) == 0);
                } else {
                    run_next = (execute_simple(and_token) == 0);
                }
            }
            and_token = strtok_r(NULL, "&&", &subptr);
        }
        token = strtok_r(NULL, ";", &saveptr);
    }
}

int main() {
    char input[MAX_CMD_LEN];

    signal(SIGINT, handle_sigint);

    while (shell_running) {
        display_prompt();
        if (!fgets(input, sizeof(input), stdin)) break;
        if (strcmp(input, "\n") == 0) continue;

        add_to_history(input);
        process_input(input);
    }
