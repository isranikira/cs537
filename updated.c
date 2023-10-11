//wsh.c


#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>

//process struct
typedef struct process{
    struct process *next;      //next process to do
    char **argv; 
    pid_t pid;
    char completed;             //keep track of process completion
    char stopped;               //true when stopped
    int status;
} process;

//pipeline of processes
typedef struct job{
    struct job *next;   //pointer to next active job
    char *command;      //command line
    process *first_process; //list of processes in this job
    pid_t pgid;      //what the process group id is 
    int id;         //what its id is (will loop this to find next id)
    int curr_bg;    //represents if procces is in the background
    char notified;
    struct termios tmodes;      //saved terminal modes might not use
    int stdin, stdout, stderr; //not sure if i need stderr
}job;

job *first_job = NULL;
job *current_job = NULL;
int next_job_id = 1;

char *buffer;
char *path = "/bin";
size_t bufsize = 256;

//global variables for the shell init
pid_t shell_pgid;
struct termios shell_tmodes;
int shell_terminal;
int shell_is_interactive;

//signal hangling 
//volatile sig_atomic_t sig_child_flag = 0;
volatile sig_atomic_t sig_stop_flag = 0;

//handler for SIGCHLD
void sig_child_handler(int sig_child){
    //int sig_child_flag = 1;
    int status;
    waitpid(WAIT_ANY, &status, WUNTRACED|WNOHANG);
}
//utility functions for operating job objects

//find active job
job *
find_job (pid_t pgid){
    job *j;
    for (j = first_job; j; j=j->next){
        if(j->pgid == pgid)
            return j;
    }
    return NULL;
}

//return true if all process in job is stooped or completed
int
job_is_stopped (job *j){
    process *p;
    for(p= j->first_process; p; p=p->next){
        if(!p->completed && !p->stopped)
            return 0;
    }
    return 1;
}

//return true if all processes in job have completed
int
job_is_completed(job *j){
    process *p;
    for(p = j->first_process; p; p = p->next){
        if(!p->completed)
            return 0;
    }
    return 1;
}

/* Store the status of the process pid that was returned by waitpid.
   Return 0 if all went well, nonzero otherwise.  */

int
mark_process_status (pid_t pid, int status)
{
  job *j;
  process *p;

  if (pid > 0)
    {
      /* Update the record for the process.  */
      for (j = first_job; j; j = j->next)
        for (p = j->first_process; p; p = p->next)
          if (p->pid == pid)
            {
              p->status = status;
              if (WIFSTOPPED (status))
                p->stopped = 1;
              else
                {
                  p->completed = 1;
                  if (WIFSIGNALED (status))
                    fprintf (stderr, "%d: Terminated by signal %d.\n",
                             (int) pid, WTERMSIG (p->status));
                }
              return 0;
             }
      return -1;
    }
  else if (pid == 0) //|| errno == ECHILD took out
    /* No processes ready to report.  */
    return -1;
  else {
    /* Other weird errors.  */
    return -1;
  }
}


/* Check for processes that have status information available,
   blocking until all processes in the given job have reported.  */

void
wait_for_job (job *j)
{
  int status;
  pid_t pid;

  do
    pid = waitpid (WAIT_ANY, &status, WUNTRACED);
  while (!mark_process_status (pid, status)
         && !job_is_stopped (j)
         && !job_is_completed (j));
}

/* Notify the user about stopped or terminated jobs.
   Delete terminated jobs from the active job list.  */

void
do_job_notification (void)
{
  job *j, *jlast, *jnext;

  /* Update status information for child processes.  */

  jlast = NULL;
  for (j = first_job; j; j = jnext)
    {
      jnext = j->next;

      /* If all processes have completed, tell the user the job has
         completed and delete it from the list of active jobs.  */
      if (job_is_completed (j)) {
        if (jlast)
          jlast->next = jnext;
        else
          first_job = jnext;
        //free_job (j);
      }

      /* Notify the user about stopped jobs,
         marking them so that we won’t do this more than once.  */
      else if (job_is_stopped (j) && !j->notified) {
        j->notified = 1;
        jlast = j;
      }

      /* Don’t say anything about jobs that are still running.  */
      else
        jlast = j;
    }
}


/* Put a job in the background.  If the cont argument is true, send
   the process group a SIGCONT signal to wake it up.  */

void
put_job_in_background (job *j, int cont)
{
    j->curr_bg = 1;
    signal(SIGINT, SIG_IGN); 
    signal(SIGTSTP, SIG_IGN);
  /* Send the job a continue signal, if necessary.  */
    if (cont){
        if (kill (-j->pgid, SIGCONT) < 0)
        perror ("kill (SIGCONT)");
    } 
}

void
put_job_in_foreground (job *j, int cont)
{
  /* Put the job into the foreground.  */
  tcsetpgrp (shell_terminal, j->pgid);

  /* Send the job a continue signal, if necessary.  */
  if (cont)
    {
      tcsetattr (shell_terminal, TCSADRAIN, &j->tmodes);
      if (kill (- j->pgid, SIGCONT) < 0)
        perror ("kill (SIGCONT)");
    }

  /* Wait for it to report.  */
  wait_for_job (j);

  /* Put the shell back in the foreground.  */
  tcsetpgrp (shell_terminal, shell_pgid);

  /* Restore the shell’s terminal modes.  */
  tcgetattr (shell_terminal, &j->tmodes);
  tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}



void setup(){

    //give space for the buffer
    buffer = (char *)malloc(bufsize * sizeof(char));
    if( buffer == NULL)
    {
        perror("Unable to allocate buffer");
        exit(1);
    }
    // // Initialize jobs array
    // for (int i = 0; i < 256; i++) {
    //     job[i].id = -1;
    // }

    //ADD CODE do i need to handle end of file for both modes 
}

/* will set some global vars and make sure forks will go offf our 
*  current shell and not make its own one
*/
void init_shell(){
    //make sure we are running interactively
    shell_terminal = STDIN_FILENO;
    shell_is_interactive = isatty(shell_terminal);

    if(shell_is_interactive){
        //loop until we are in the fg
        while(tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp())){
            kill(-shell_pgid, SIGTTIN); //kills all processes with pid if pid negative
        }

        /* Ignore interactive and job-control signals.  */
        signal (SIGINT, SIG_IGN); //will need to set all these back to defualt for forks
        signal (SIGQUIT, SIG_IGN);
        signal (SIGTSTP, SIG_IGN);
        signal (SIGTTIN, SIG_IGN);
        signal (SIGTTOU, SIG_IGN);
        signal (SIGCHLD, SIG_IGN);

        //put the shell in its own process group 
        shell_pgid = getpid();
        if(getpid() != getsid(0)){
            if(setpgid(shell_pgid, shell_pgid) < 0) //returns -1 error
            {
                char proc_grp_err[256] = "Couldn't put the shell in its own process group";
                write(STDOUT_FILENO, proc_grp_err, strlen(proc_grp_err));
                exit (-1);
            }
        }
        // Grab control of the terminal.  set to foreground
        tcsetpgrp (shell_terminal, shell_pgid);

        /* Save default terminal attributes for shell.  */
        tcgetattr (shell_terminal, &shell_tmodes);
    }
}

void launch_process (process *p, pid_t pgid,
                int infile, int outfile, int errfile,
                int curr_bg, char *curr_path)
{
  pid_t pid;

  if (shell_is_interactive)
    {
      /* Put the process into the process group and give the process group
         the terminal, if appropriate.
         This has to be done both by the shell and in the individual
         child processes because of potential race conditions.  */
      pid = getpid ();
      if (pgid == 0) pgid = pid;
      setpgid (pid, pgid);
      if (!curr_bg)
        tcsetpgrp (shell_terminal, pgid);

      /* Set the handling for job control signals back to the default.  */
      signal (SIGINT, SIG_DFL);
      signal (SIGQUIT, SIG_DFL);
      signal (SIGTSTP, SIG_DFL);
      signal (SIGTTIN, SIG_DFL);
      signal (SIGTTOU, SIG_DFL);
      signal (SIGCHLD, SIG_DFL);
    }

  /* Set the standard input/output channels of the new process.  */
  if (infile != STDIN_FILENO)
    {
      dup2 (infile, STDIN_FILENO);
      close (infile);
    }
  if (outfile != STDOUT_FILENO)
    {
      dup2 (outfile, STDOUT_FILENO);
      close (outfile);
    }
  if (errfile != STDERR_FILENO)
    {
      dup2 (errfile, STDERR_FILENO);
      close (errfile);
    }

  /* 
  ec the new process.  Make sure we exit.  */
  execvp (curr_path, p->argv);
  perror ("execvp");
  exit (1);
}

char *get_path(process *p){
    char path_usr[256] = "/usr/bin/";
    char path[256] = "/bin/";
    char *curr_path = (char *)malloc(256);

    if (curr_path == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    strcat(path_usr, p->argv[0]);
    strcat(path, p->argv[0]);

    if(access(path_usr, X_OK) != 0){
        if(access(path, X_OK) != 0){
            char not_found[256] = "Command was not found\n";
            write(STDOUT_FILENO, not_found, strlen(not_found));
            free(curr_path);
            return "0";
        }
        else{
            strcpy(curr_path, path);
        }
    }
    else{
        strcpy(curr_path, path_usr);
    }
    return curr_path;
}

void sig_handler(int signo){
    //int status;
}

void launch_job(job *j){
    process *p;
    pid_t pid;
    int mypipe[2], infile, outfile;

    infile = j->stdin;

    //loop through 
    for (p = j->first_process; p; p=p->next){
        //set up pipes if necessary
        if(p->next){
            if(pipe (mypipe) <0){
                perror ("pipe");
                exit(1);
            }
            outfile = mypipe[1];
        }
        else{
            outfile = j->stdout;
        }

        char *curr_path = get_path(p);
        if(strcmp(curr_path, "0") == 0){
            //path not found continue or break?
            break;
        }
        //fork the child processes
        pid = fork();
        if(pid == 0){
            //this is the child process
            launch_process(p,j->pgid, infile, outfile, j->stderr, j->curr_bg, curr_path);
        }
        else if(pid < 0){
            //the fork failed
            perror("fork");
            exit(1);
        }
        else{
            //this is the parent process
            p->pid = pid;
            if(shell_is_interactive){
                if(!j->pgid){
                    j->pgid = pid;
                }
                setpgid(pid, j->pgid);
                if(!(j->curr_bg)){
                    put_job_in_foreground(j, 0);
                }
                else{
                    signal(SIGCHLD, sig_handler);
                    put_job_in_background(j, 0);
                }
            }
        }
        //clean up after pipes
        if(infile != j->stdin){
            close(infile);
        }
        if(outfile != j->stdout){
            close(outfile);
        }
        infile = mypipe[0];
    }

    if(!shell_is_interactive){
        wait_for_job(j);
    }
    else if(j->curr_bg){
        put_job_in_background(j, 0);
    }
    else{
        put_job_in_foreground(j, 0);
    }
}


//TODO: this needs to parse the 
void parse_process(char* command, char* args[]){
    buffer[strcspn(buffer, "\n")] = '\0';
    char* token = strtok(buffer, " ");
    int arg_index = 0;

     //get the comand
    strcpy(command, token);

    // Extract arguments
    while (token != NULL) {
        token = strtok(NULL, " ");
        if (token != NULL) {
            args[arg_index] = strdup(token);
            arg_index = arg_index + 1;
        }
    }

    // args[arg_index] = '\0';
}

process *create_process(char *command, char **args){
    //handle the first process
    process *first_process = NULL; //need to FREE
    process *last_process = NULL; //need to FREE
    process *current_process = NULL; //need to FREE
    
    
    int arg_count = 0;
    int curr_amount = 0;
    int is_first = 0;

    int start_count = 0;
    //do-while loop through every args in args
    do{
        current_process = (process *)malloc(sizeof(process)); //allocate new mem for this proc
        //loop through args until hit null or | incrementing arg count
        while((args[arg_count] != NULL) && (strcmp(args[arg_count], "|") != 0)){
            curr_amount++;
            arg_count++;
        } 
        //if | change it to null
        if(args[arg_count] != NULL){
            args[arg_count] = NULL;
        }
        //assign memory for the args array
        char *proc_args[256]; // need to FREE
        for (int i = 0; i < curr_amount+1; i++) {
            proc_args[i] = (char*)malloc(sizeof(args[i]));
        }
        //if it is the first one the command is outside of array
        if(first_process == NULL){
            proc_args[0] = command;
            is_first = 1;
        }
        //copy over the array from args job to the proc array
        for(int i=0; i<curr_amount; i++){
            strcpy(proc_args[is_first], args[start_count]);
            start_count++; //update the new start count
            is_first++;
        }
        proc_args[start_count+1] = NULL; //null terminator
        start_count++; //go past the nul charecter
        is_first = 0;
        
        //assign the cur proc to point to proc array
        current_process->argv = proc_args;
        current_process->completed = 0;
        current_process->stopped = 0;
        //if ther first proc not defined make it the curr job
         if(first_process == NULL){
            first_process = current_process;
            last_process = current_process;
        }
        else{
            last_process->next = current_process;
            last_process = current_process;
        }
        //then update the last job to point to curr job
        //else update the last jobs pointer then ake last job curr job poniter
        
        

    }while (args[arg_count] != NULL);
    
    //TODO: might need to be a zero
    //create the next process until "|" or null
    // memcpy(first_process, current_process, sizeof(process));
    //         for(int j = 0; j<curr_count; j++){
    //             strcpy(first_proc_args[j], proc_args[j]);
    //         }

    return first_process;


    //allocate memory for argv and use command and 
}

job *create_job(char *command, char ** args, int is_bg){
    job *j = (job *)malloc(sizeof(job));
    job *comp_job = NULL;
    j->next = NULL;
    j->command = strdup(command);
    //parse the args in the create process func
    j->first_process = create_process(command, args);
    j->pgid = 0;
    j->curr_bg = is_bg;
    int need_id = 1;
    int curr_id = 0;

    //find the smallest id availible
    if(first_job == NULL){
        j->id = 1;
    }
    else{
        while(need_id){
            curr_id = curr_id + 1;
            need_id = 0;    //assume that this run will find correct id
            for (comp_job = first_job; comp_job; comp_job=comp_job->next){
                if(comp_job->id == curr_id){
                    need_id = 1; //found a match
                }
            }
        }
        j->id = curr_id;
    }

    //initialize stdin, stdout, stderr
    j->stdin = STDIN_FILENO;
    j->stdout = STDOUT_FILENO;
    j->stderr = STDERR_FILENO;

    launch_job(j);
    return j;
}

int change_dir(char *arg){
    //TODO ARGS MIGHT NEED TO BE SHIFTED OVER ONE
    //make sure  to have at least one parameter
    if (arg == 0) { 
        fprintf(stderr, "cd: Needs one more argument\n");
        return 0; // Error
    }
    // Use chdir to change the current directory returns 0 on success
    if (chdir(arg) != 0) {
        fprintf(stderr, "cd: Unable to change to directory requested\n");
        return 0; // Error
    }
    return 0; // Success
}

int move_job(char* args[], int curr_fg){
    //check the correct amount of parameters are passed to it
    if(args[1] != NULL){
        fprintf(stderr, "move_process: Passed in too many parameters\n");
        return 1; //Error
    }
    if(first_job == NULL){
        fprintf(stderr, "no current jobs\n");
        return 1;
    }
    //if no arg is passed through need tp use largest id
    int job_id = 0;
    job *j;
    job *change_job;
    //start with the largset job and loop through each one
    if(args[0] == NULL){
        for(j = first_job; j; j=j->next){
            if(j->id > job_id){
                job_id = j->id;
                change_job = j;
            }
        }
    }
    //else if there is an arg set that to job id
    else{
        for(j = first_job; j; j=j->next){
            if(j->id == atoi(args[0])){
                job_id = j->id;
                change_job = j;
            }
        }
        if(job_id == 0){
            fprintf(stderr, "no current jobs with id passed in\n");
            return 1;
        }
    }
    //if fg is used bring RUNNING bg to fg
    if(curr_fg && change_job->curr_bg){
        //tcsetpgrp(STDIN_FILENO, change_job); //bring to the foreground
        put_job_in_foreground(change_job, 1);
    }
    //resumes job in the bg
    else if(!curr_fg){
        printf("try to go to bg");
        put_job_in_background(change_job, 1);
    }
    //else error message
    else{
        //error: printf(stderr, "unable to change fg or bg\n");
        return 1;
    }
    return 1; //Should not reach this if found process 
}

void list_all_jobs(){
    //start with the first_job - check that it is not null
    job *curr_job;
    process *curr_proc;
    int curr_arg = 0;
    if(first_job == NULL){
        printf("there are no jobs in the background\n");
        return;
    }
    ////for each job print their id
    for(curr_job = first_job; curr_job; curr_job=curr_job->next){
        printf("%d: ", curr_job->id);
        //loop through each of their processes
        if(curr_job->first_process == NULL){
            return;
        }
        for(curr_proc = curr_job->first_process; curr_proc; curr_proc=curr_proc->next){
            //for the process print the args while it is not null
            while(curr_proc->argv[curr_arg] != NULL){
                printf("%s ", curr_proc->argv[curr_arg]);
            }
            //if there is a next process print the | charecter 
            if((curr_proc = curr_proc->next) != NULL){
                 printf("| ");
            }
        }
        //else if it was started in the background print the ampersand
        if(curr_job->curr_bg){
            printf("&");
        }
        printf("\n");
    }
}


void read_in_prompt(char* command, char *args[]){
    while(1){
            //print the prompt to the user
            printf("wsh> ");
            getline(&buffer,&bufsize,stdin);
            parse_process(command, args);
            
            
            //chack what the command is 
            if (strcmp(command, "exit") == 0) {
                break;
            } 
            else if (strcmp(command, "cd") == 0) {
                int result = change_dir(args[0]);
                if( result != 0){
                     fprintf(stderr, "cd: Failed to change directory\n");
                }
            } 
            else if (strcmp(command, "jobs") == 0) {
                list_all_jobs();
            } 
            else if(strcmp(command, "fg") == 0){
                move_job(args, 1);
            }
            else if(strcmp(command, "bg") == 0){
                move_job(args, 0);
            }
            else if(strcmp(command, "jobs") == 0){
                //list_all_jobs(command, args); //need the command to pring and args to tell if bg init
            }
            else{
                //find if the job is supposed to be in the background or not
                int is_bg = 0; //one when the command should run in the fg
                int j = 0;
                while(args[j] != NULL){
                    //if true needs to be in the bg
                    if(strcmp(args[j], "&") == 0){
                        is_bg = 1;
                        args[j] = NULL;
                    } 
                    j = j+1;
                }
                
                //TODO gives segmentation fault
                //create the jobs and processes
                if(first_job == NULL){
                    current_job = create_job(command, args, is_bg);
                    first_job = current_job;
                }
                else{
                    job *new_job = create_job(command, args, is_bg);
                    current_job->next = new_job;
                    current_job = new_job;
                }
                //launch the current job here
                
            }

            int j =0;
            while(args[j] != NULL){
                args[j] = NULL;
                j = j+ 1;
            }
            //read in the prompt from the user and determine what to do 
        }
}

int main(int argc, char *argv[]){
    char *command;
    command = malloc(256* sizeof(*command));
    char *args[256];

    // Initialize the args array
    for (int i = 0; i < 256; i++) {
        args[i] = NULL;
    }

    //setup for the buffer 
    setup();

    //setup shell
    init_shell();

    //if the arg amount is two go to batch mode and run from that
    //skip the while loop
    if(argc == 1){
        read_in_prompt(command, args);
        //while loop continue to prompt the 
    }
    else if(argc == 2){
        char* filename = argv[1]; 
        //do some checking to make sure that file is accurate
        FILE *file_in = fopen(filename, "r");

        while(fgets(buffer,bufsize,file_in)){
            //ADD CODE handle that line
            // parse_process(command, args);
            // handle_prompt(command, args);
        }
    }

    return 0;

    //while loop for however many commands in the script
}
