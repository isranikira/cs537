#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>

// process struct
typedef struct process {
    struct process *next;
    char **argv; 
    pid_t pid;
    char completed;
    char stopped;
    int status;
} process;

// pipeline of processes
typedef struct job {
    struct job *next;
    char *command;
    process *first_process;
    pid_t pgid;
    int id;
    int curr_bg;
    char notified;
    struct termios tmodes;
    int stdin, stdout, stderr;
} job;

job *first_job = NULL;
job *current_job = NULL;
int next_job_id = 1;

char *buffer;
size_t bufsize = 256;

pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

void init_shell();
void read_in_prompt(char* command, char *args[]);
void handle_prompt(char* command, char* args[]);
void launch_job(job *j);

int main(int argc, char *argv[]) {
    char *command = malloc(256 * sizeof(*command));
    char *args[256];
    for (int i = 0; i < 256; i++) {
        args[i] = NULL;
    }

    buffer = (char *)malloc(bufsize * sizeof(char));
    if (!buffer) {
        perror("Unable to allocate buffer");
        exit(1);
    }

    init_shell();

    if (argc == 1) {
        read_in_prompt(command, args);
    }
    // TODO: Handling of batch mode if needed

    free(buffer);
    free(command);
    return 0;
}

void init_shell() {
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if (shell_is_interactive) {
        while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
        
        signal(SIGINT, SIG_IGN);
        // ... (other signals)
        
        shell_pgid = getpid();
        if (setpgid(shell_pgid, shell_pgid) < 0) {
            perror("Couldn't put the shell in its own process group");
            exit(1);
        }

        tcsetpgrp(shell_terminal, shell_pgid);
        tcgetattr(shell_terminal, &shell_tmodes);
    }
}

void read_in_prompt(char* command, char *args[]) {
    while (1) {
        printf("wsh> ");
        getline(&buffer, &bufsize, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';
        char *token = strtok(buffer, " ");
        strcpy(command, token);
        int arg_index = 0;

        while ((token = strtok(NULL, " ")) != NULL) {
            args[arg_index++] = strdup(token);
        }

        handle_prompt(command, args);
        
        for (int i = 0; i < arg_index; i++) {
            free(args[i]);
            args[i] = NULL;
        }
    }
}

void handle_prompt(char* command, char* args[]) {
    int is_bg = 0;
    if (args[0] && strcmp(args[0], "&") == 0) {
        is_bg = 1;
        args[0] = NULL; // Remove & from arguments
    }

    if (strcmp(command, "exit") == 0) {
        exit(0);
    } else if (strcmp(command, "cd") == 0) {
        if (args[0] && chdir(args[0]) != 0) {
            perror("cd");
        }
    } else {
        job *j = (job *)malloc(sizeof(job));
        j->command = strdup(command);
        j->first_process = (process *)malloc(sizeof(process));
        j->first_process->argv = args;
        j->curr_bg = is_bg;
        j->next = NULL;

        if (!first_job) {
            first_job = j;
            current_job = j;
        } else {
            current_job->next = j;
            current_job = j;
        }

        launch_job(j);
    }
}

void launch_job(job *j) {
    process *p = j->first_process;
    pid_t pid = fork();
    if (pid == 0) { // Child
        execvp(p->argv[0], p->argv);
        perror("execvp");
        exit(1);
    } else if (pid < 0) {
        perror("fork");
    } else { // Parent
        p->pid = pid;
        if (j->curr_bg) {
            printf("[%d] %d\n", next_job_id++, pid);
        } else {
            waitpid(pid, NULL, 0);
        }
    }
}
