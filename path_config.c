// ... (rest of the code)

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
    
    // Check if the command isn't 'cd' or 'exit' and prepend /bin/ to the command.
    if (strcmp(p->argv[0], "cd") != 0 && strcmp(p->argv[0], "exit") != 0) {
        char path[1024];
        snprintf(path, sizeof(path), "/bin/%s", p->argv[0]);
        p->argv[0] = path;
    }
    
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

// ... (rest of the code)
