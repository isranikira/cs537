#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_ARG_SIZE 256

// Process struct
typedef struct process {
    struct process* next;
    char** argv;
    pid_t pid;
    char completed;
    char stopped;
    int status;
} process;

// Job struct
typedef struct job {
    struct job* next;
    char* command;
    process* first_process;
    pid_t pgid;
    char notified;
    int bg; // Whether the job is in the background
} job;

char *buffer = NULL;
size_t bufsize = 256;

void setup() {
    buffer = (char *)malloc(bufsize * sizeof(char));
    if(buffer == NULL) {
        perror("Unable to allocate buffer");
        exit(1);
    }
}

void read_in_prompt(char** command, char* args[]) {
    printf("wsh> ");
    getline(&buffer, &bufsize, stdin);
    buffer[strcspn(buffer, "\n")] = '\0'; // Remove newline
    *command = strtok(buffer, " ");

    int i = 0;
    while ((*command) != NULL) {
        args[i++] = *command;
        *command = strtok(NULL, " ");
    }
    args[i] = NULL;
}

job* create_job(char* command, char** args) {
    job* j = (job *)malloc(sizeof(job));
    j->command = strdup(command);
    j->next = NULL;
    j->bg = 0;
    for (int i = 0; args[i]; i++) {
        if (strcmp(args[i], "&") == 0) {
            j->bg = 1;
            args[i] = NULL;
            break;
        }
    }

    process* p = (process *)malloc(sizeof(process));
    p->argv = args;
    p->next = NULL; // Only one process for now, can be expanded for pipelines

    j->first_process = p;

    return j;
}

void launch_job(job* j) {
    process* p = j->first_process;
    if (!p) return;

    int pid = fork();
    if (pid == 0) { // Child process
        char path[MAX_ARG_SIZE];
        snprintf(path, sizeof(path), "/bin/%s", p->argv[0]);
        execvp(path, p->argv);
        perror("execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) { // Error in fork
        perror("fork");
    } else { // Parent process
        p->pid = pid;
        if (!j->bg) {
            waitpid(pid, NULL, 0);
        }
    }
}

void handle_prompt(char* command, char* args[]) {
    if (strcmp(command, "exit") == 0) {
        free(buffer);
        exit(0);
    } else if (strcmp(command, "cd") == 0) {
        if (!args[1]) {
            fprintf(stderr, "cd: Needs one more argument\n");
        } else {
            if (chdir(args[1]) != 0) {
                perror("cd");
            }
        }
    } else {
        job* j = create_job(command, args);
        launch_job(j);
        // Cleanup
        free(j->command);
        free(j->first_process);
        free(j);
    }
}

int main(int argc, char* argv[]) {
    char* command;
    char* args[MAX_ARG_SIZE];

    setup();

    while (1) {
        read_in_prompt(&command, args);
        handle_prompt(command, args);
    }

    return 0;
}
