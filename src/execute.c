#include "shell.h"
#include <fcntl.h>

#define MAX_JOBS 64

typedef struct {
    int id;
    pid_t pid;
    char cmd[MAX_LEN];
    int active;
} BgJob;

static BgJob job_list[MAX_JOBS];
static int next_job_id = 1;

static void add_job(pid_t pid, const char* cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!job_list[i].active) {
            job_list[i].id = next_job_id++;
            job_list[i].pid = pid;
            strncpy(job_list[i].cmd, cmd ? cmd : "", MAX_LEN - 1);
            job_list[i].cmd[MAX_LEN - 1] = '\0';
            job_list[i].active = 1;
            printf("[%d] %d\n", job_list[i].id, pid);
            return;
        }
    }
    fprintf(stderr, "Maximum background jobs reached.\n");
}

void reap_zombies(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (job_list[i].active && job_list[i].pid == pid) {
                printf("[%d] Done    %s\n", job_list[i].id, job_list[i].cmd);
                job_list[i].active = 0;
                break;
            }
        }
    }
}

void print_jobs(void) {
    reap_zombies();
    int found = 0;
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_list[i].active) {
            printf("[%d] %d Running    %s\n", job_list[i].id, job_list[i].pid, job_list[i].cmd);
            found = 1;
        }
    }
    if (!found) {
        printf("No active background jobs.\n");
    }
}

static void setup_redirection_and_exec(char** cmd_args) {
    char* exec_args[MAXARGS + 1];
    int j = 0;

    for (int i = 0; cmd_args[i] != NULL; i++) {
        if (strcmp(cmd_args[i], "<") == 0) {
            if (cmd_args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected input file after '<'\n");
                exit(1);
            }
            int fd = open(cmd_args[i + 1], O_RDONLY);
            if (fd < 0) {
                perror(cmd_args[i + 1]);
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            i++;
        } else if (strcmp(cmd_args[i], ">") == 0) {
            if (cmd_args[i + 1] == NULL) {
                fprintf(stderr, "Syntax error: expected output file after '>'\n");
                exit(1);
            }
            int fd = open(cmd_args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror(cmd_args[i + 1]);
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            i++;
        } else {
            exec_args[j++] = cmd_args[i];
        }
    }
    exec_args[j] = NULL;

    if (j == 0) exit(0);

    execvp(exec_args[0], exec_args);
    perror("Command not found");
    exit(1);
}

int execute_bg(char* arglist[], int is_bg, const char* raw_cmd) {
    char* work[MAXARGS + 1];
    char** cmds[MAXARGS];
    int num_cmds = 0;
    int k = 0;

    cmds[num_cmds++] = &work[0];
    for (int i = 0; arglist[i] != NULL; i++) {
        if (strcmp(arglist[i], "|") == 0) {
            work[k++] = NULL;
            if (arglist[i + 1] != NULL) {
                cmds[num_cmds++] = &work[k];
            }
        } else {
            work[k++] = arglist[i];
        }
    }
    work[k] = NULL;

    int prev_fd = -1;
    int pipefd[2];
    pid_t pids[MAXARGS];

    for (int i = 0; i < num_cmds; i++) {
        if (i < num_cmds - 1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                return 1;
            }
        }

        pid_t cpid = fork();
        if (cpid < 0) {
            perror("fork failed");
            exit(1);
        } else if (cpid == 0) {
            if (prev_fd != -1) {
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if (i < num_cmds - 1) {
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }
            setup_redirection_and_exec(cmds[i]);
        } else {
            pids[i] = cpid;
            if (prev_fd != -1) {
                close(prev_fd);
            }
            if (i < num_cmds - 1) {
                close(pipefd[1]);
                prev_fd = pipefd[0];
            }
        }
    }

    if (is_bg) {
        add_job(pids[num_cmds - 1], raw_cmd);
        return 0;
    }

    int status = 0;
    for (int i = 0; i < num_cmds; i++) {
        waitpid(pids[i], &status, 0);
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int execute(char* arglist[]) {
    return execute_bg(arglist, 0, arglist[0]);
}

int handle_builtin(char** arglist) {
    if (strcmp(arglist[0], "exit") == 0) {
        printf("Exiting shell...\n");
        exit(0);
    }
    else if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            fprintf(stderr, "cd: missing directory argument\n");
            return 1;
        } else if (chdir(arglist[1]) != 0) {
            perror("cd");
            return 1;
        }
        return 0;
    }
    else if (strcmp(arglist[0], "help") == 0) {
        printf("Built-in commands:\n");
        printf("  cd <directory>  - change the current directory\n");
        printf("  exit            - exit the shell\n");
        printf("  help            - show this help message\n");
        printf("  history         - display command history\n");
        printf("  jobs            - list active background jobs\n");
        printf("  set             - display all shell variables\n");
        return 0;
    }
    else if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 0;
    }
    else if (strcmp(arglist[0], "set") == 0) {
        print_shell_vars();
        return 0;
    }
    else if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 0;
    }
    return -1;
}
