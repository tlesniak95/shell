#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <termios.h>
#include <signal.h>

/* Data structure for a single process
This structe is a singly linked list
    It is importatnt in the case of a pipeline
the char **argv is for execvp. It is an array of strings represeting
    the arguments for the command to be executed
pid will be useful for sending signals or waiting for the process to finish
The completed and stopped fields are used to keep track of the status of the process
    completed is true if the process has completed
    stopped is true if the process has stopped (user pressed Ctrl-Z)
    status is the reported status value. This will be
    updated by the waitpid() system call
This struct will allow the shell to manage individual processes in a pipeline
or when managing background processes

*/
typedef struct process
{
    struct process *next; // next process in pipeline
    char **argv;          // for exec
    pid_t pid;            // process ID
    char completed;       // true if process has completed
    char stopped;         // true if process has stopped
    int status;           // reported status value
} process;

/* Data structure for a job
A job structure represent a higher level of abstraction than a process
A process represents a single command or operation, a job may represent one or more processes
    that are connected. This will be useful for pipeplining
This is a linked lsist where next is the pointer to the next job
char command represent the entire command line input for the job
first process is the head fo the list of processes in the job
pgid is the used to identify the process group that can be used
    to then send signals to the entire group
tmodes is used to save the terminal modes when the job is in the foreground.
    this allows the the terminal settings to be restored
    when the job is moved back to the foreground
stdin, stdout, and stderr are the file descriptors for the standard input,
    this is used for redirection of input and output
*/
typedef struct job
{
    struct job *next;          // next active job
    int index;                 // index of the job
    char *command;             // command line, used for messages
    int background;            // true if job is in background
    process *first_process;    // list of processes in this job
    pid_t pgid;                // process group ID
    char notified;             // true if user told about stopped job
    struct termios tmodes;     // saved terminal modes
    int stdin, stdout, stderr; // standard i/o channels
} job;

job *first_job = NULL; // This initializes the first job to NULL
job *currentFG = NULL; // This initializes the current foreground job to NULL

/*
This function is a way to search through the linked list of jobs to find
    a job that has a process group ID that matches
This will be usefeul for foreground and background switching and signal handling
*/
job *find_job(int idx)
{
    job *j;
    for (j = first_job; j; j = j->next)
        if (j->index == idx)
            return j;
    return NULL;
}
/*
this function will be used for the built in jobs command
    It returns 1 if job is stopped, 0 otherwise
*/

int job_is_stopped(job *j)
{
    process *p;
    for (p = j->first_process; p; p = p->next)
        if (!p->completed && !p->stopped)
            return 0;
    return 1;
}

/* Return 1 if all processes in the job have completed.  */
int job_is_completed(job *j)
{
    process *p;

    for (p = j->first_process; p; p = p->next)
        if (!p->completed)
            return 0;
    return 1;
}

void update_jobs()
{
    job *j = first_job, *prev = NULL;
    while (j != NULL)
    {
        pid_t result = waitpid(j->pgid, NULL, WNOHANG); // WNOHANG means return immediately if no child has exited
        if (result == 0) //no changes in state
        {
            // Job is still running
            prev = j;
            j = j->next;
        }
        else if (result == j->pgid)
        {
            // Job has finished
            if (prev != NULL)
            {
                prev->next = j->next;
            }
            else
            {
                first_job = j->next;
            }
            job *to_free = j;
            j = j->next;
            free(to_free->command);
            free(to_free);
        }
        else
        {
            // Error case, might want to handle more gracefully in production code
            perror("waitpid");
            exit(EXIT_FAILURE);
        }
    }
}

int nextSmallestIndex()
{
    int smallest = 1;
    job *j = first_job;

    // create an array to mark indicies that are in use
    int in_use[256] = {0};

    while (j != NULL)
    {
        in_use[j->index] = 1;
        j = j->next;
    }

    // Now find the smallest index not in use.
    while (in_use[smallest])
    {
        smallest++;
    }

    return smallest;
}

int find_greatest_index()
{
    int greatest = -1; // Initialized to -1 to account for the possibility of no jobs.
    job *j = first_job;

    while (j != NULL)
    {
        if (j->index > greatest)
        {
            greatest = j->index;
        }
        j = j->next;
    }

    return greatest;
}

void print_jobs()
{
    job *current = first_job;

    while (current)
    {      
            printf("%d: %s\n", current->index, current->command);
        current = current->next;
    }
}

/*
commands: a 3d array representing piped commands and their arguuments
num_commands is the number of commands
background is a flag that indicates whether the job is running in the background
*/
void execute_pipeline(char ***commands, int num_commands, int background)
{
    int i; // Loop counter
    // in fd is the file descriptor used to redirect output of a command
    int in_fd = STDIN_FILENO; // Start with standard input
    // fd is an array that will hold the file descriptors for the pipe I/O
    int fd[2];
    // first_pid will be used to store the process group ID for all processes
    pid_t pid, first_pid = 0;
    // dynamically allocating memory for the job
    job *current_job = malloc(sizeof(job));
    current_job->next = NULL;
    current_job->first_process = NULL;
    current_job->background = background;

    // loop to itterate over each command in the pipeline
    // for all but the last command we create a pipe to pass output of
    // current command to the next command in the pipeline
    for (i = 0; i < num_commands; i++)
    {
        if (i < num_commands - 1)
        {
            if (pipe(fd) == -1) // Create the pipe
            {
                perror("pipe");
                exit(1);
            }
        }
        // create a new process for the current command
        pid = fork();
        if (pid == 0)
        {
            // child process
            // if not, then there is input from a previous command
            if (in_fd != STDIN_FILENO)
            {
                // duplicate in_fd to stdin and close in_fd
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            // for all but the last command we redirect the standard output to
            // the write end of the pipe
            if (i < num_commands - 1)
            {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[1]);
                close(fd[0]);
            }
            // execute the command
            execvp(commands[i][0], commands[i]);
            perror("execvp");
            exit(1);
            // if this, then the fork failed
        }
        else if (pid < 0)
        {
            perror("fork");
            exit(1);
        }
        else
        { // In the parent
            if (i == 0)
            {
                first_pid = pid;
            }

            // if its the first command then store its PID as the group
            setpgid(pid, first_pid); // Add the child to the process group of the first child
            // if the pipeline is not meant to run in the background and its the last
            // command then we wait for the child to finish
            if (!background && i == num_commands - 1)
            {
                int status;
                tcsetpgrp(STDIN_FILENO, pid);
                currentFG = current_job;
                waitpid(pid, &status, WUNTRACED);
                if (WIFSTOPPED(status))
                    {currentFG->background = 1;
                        currentFG->index = nextSmallestIndex();

                        if (first_job == NULL)
                        {
                            first_job = currentFG;
                        }
                        else
                        {
                            job *lastJob = first_job;
                            while (lastJob->next != NULL)
                            {
                                lastJob = lastJob->next;
                            }
                            lastJob->next = currentFG;
                        }
                    }

                currentFG = NULL;
                tcsetpgrp(STDIN_FILENO, getpgrp());
            }


            // Close write end and move read end for next command
            if (in_fd != STDIN_FILENO)
            {
                close(in_fd);
            }
            // for all but the last command close the write end of the current pipe and move
            // the read end into in_fd for the next iteration
            if (i < num_commands - 1)
            {
                close(fd[1]);
                in_fd = fd[0];
            }
            // dynamically allocate memory for the process
            process *proc = malloc(sizeof(process));
            proc->argv = commands[i];
            proc->pid = pid;
            proc->completed = 0;
            proc->stopped = 0;
            proc->status = 0;
            proc->next = current_job->first_process;
            current_job->first_process = proc;
        }
    }
    // after the for loop we set the proceess group Id of the job
    current_job->pgid = first_pid;
    if (first_job == NULL)
    {
        first_job = current_job;
    }
    else
    {
        job *lastJob = first_job;
        while (lastJob->next != NULL)
        {
            lastJob = lastJob->next;
        }
        lastJob->next = current_job;
    }
    current_job->index = nextSmallestIndex();
}

void handle_sigint(int sig)
{
    if (currentFG != NULL)
    {
        kill(-currentFG->pgid, SIGINT);
    }
}
//ctrl-z
void handle_sigstp(int sig)
{

    if (currentFG != NULL) //check if there is a foreground job
    {
        kill(-currentFG->pgid, SIGTSTP); //- is used to send it to the entire process group

        currentFG->background = 1; 
        currentFG->index = nextSmallestIndex(1);

        if (first_job == NULL)
        {
            first_job = currentFG;
        }
        else
        {
            job *lastJob = first_job;
            while (lastJob->next != NULL)
            {
                lastJob = lastJob->next;
            }
            lastJob->next = currentFG;
        }
        currentFG = NULL;

        // Return terminal control to the shell
        tcsetpgrp(STDIN_FILENO, getpgrp());
    }
}

void handle_sigchld(int sig)
{
    // Use waitpid to reap zombie processes
    //The -1 means it does not wait for a specific child process but any child process that ends.

    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void init_shell()
{
    /* Ignore interactive and job-control signals. */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    struct sigaction sa;        // initialize the sigaction struct
    memset(&sa, 0, sizeof(sa)); // zero's out the struct before we use it
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = handle_sigint;
    // check Return value
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        printf("error binding SIGint handler \n");
        exit(1);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = handle_sigstp;
    if (sigaction(SIGTSTP, &sa, NULL) != 0)
    {
        printf("Error binding SIGTSTP handler \n");
        exit(1);
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) != 0)
    {
        perror("sigaction(SIGCHLD)");
        exit(1);
    }
}

/**
 * This function is designed to execute a process within the shell
 * process *p is a  pointer to the process structure which has details
 *      about the command to run and its arguments
 * pid_t pgid is the process group ID of the process
 *    this will be used for commands that are piped
 * int infile, int outfile, int errfile are the file descriptors for the
 *     standard input, output, and error. They are used to support redirection and piping
 * int foreground is a flag that indicates whether or not the process
 *      should be run in the foreground
 */

// use execvp()
int main(int argc, char *argv[])
{
    // update_jobs();
    init_shell();
    /*If line is NULL, getline() will allocate a new buffer with malloc(), or resize the
    existing buffer with realloc() if the buffer is not large enough to hold the line.*/
    char *line = NULL;
    size_t len = 0; // size_t is for an unsigned int
    ssize_t read;   // ssize_t is for unsigned int. This is important because getline() returns -1 on failure

    if (argc > 2)
    {
        printf("Incorrect number of command line arguments\n");
        exit(1);
    }

    FILE *input = (argc == 2) ? fopen(argv[1], "r") : stdin; // if argc == 2 then open the file, else open stdin
    if (input == NULL)
    {
        printf("Error in ternary conditional statement with setting input file\n");
        exit(1);
    }

    while (1)
    {
        pid_t child_pid;

        if (input == stdin)
        {
            // print the prompt
            printf("wsh> ");
        }

        /*
        ssize_t getline(char **lineptr, size_t *n, FILE *stream);
        RETURN VALUE
         On success, getline() return the number of characters read, including the delimiter  character,
         but  not  including the terminating null byte ('\0').  This value can be used to handle embedded null bytes in
        the line read, return -1 on failure
        */
        if ((read = getline(&line, &len, input)) == -1)
        {
            break;
        }

        // remove newline character
        if (read > 0 && line[read - 1] == '\n')
        {
            line[read - 1] = '\0';
        }

        char* saved_line = strdup(line); // save the line for later use

        // Splitting the line into arguments
        char *args[256];
        int i = 0;
        char *token = strtok(line, " "); // strtok() splits a string into tokens
        while (token)
        {
            args[i] = token;
            // printf("%s is the %i token\n", token, i);
            i++;
            token = strtok(NULL, " "); 
        }

        args[i] = NULL; // null-terminate the args array

        int background = 0;

        if (args[i - 1] && strcmp(args[i - 1], "&") == 0)
        {
            background = 1;
            args[i - 1] = NULL;
        }

        int has_pipe = 0;
        for (int j = 0; args[j] != NULL; j++)
        {
            if (strcmp(args[j], "|") == 0)
            {
                has_pipe = 1;
                break;
            }
        }
        if (has_pipe)
        {
            char ***commands = malloc(256 * sizeof(char *));
            int num_commands = 0;
            int command_index = 0;
            commands[num_commands] = malloc(256 * sizeof(char *));
            for (int j = 0; args[j] != NULL; j++)
            {
                if (strcmp(args[j], "|") == 0)
                {
                    commands[num_commands][command_index] = NULL;
                    num_commands++;
                    command_index = 0;
                    commands[num_commands] = malloc(256 * sizeof(char *));
                }
                else
                {
                    if (strcmp(args[j], "&") != 0)
                    {
                        commands[num_commands][command_index] = args[j];
                        command_index++;
                    }
                }
            }
            commands[num_commands][command_index] = NULL;
            num_commands++;
            commands[num_commands] = NULL;
            execute_pipeline(commands, num_commands, background);
            continue;
        }

        // if user enters exit, exit the program
        if (strcmp(args[0], "exit") == 0)
        {
            // make sure there are no other arguments
            if (args[1] != NULL)
            {
                fprintf(stderr, "ERROR exit should have no arguments\n");
                continue;
            }
            // remember that line used malloc() to allocate memory, so we need to free it
            free(line);
            exit(0);
        }
        else if (strcmp(args[0], "jobs") == 0)
        {
            print_jobs();
        }
        else if (strcmp(args[0], "fg") == 0) 
        {
            // if called with one argument then bring that job to the foreground
            job *j;
            if (args[1])
            {
                int idx = atoi(args[1]);
                j = find_job(idx);
            }
            else
            {
                int idx = find_greatest_index(1);
                j = find_job(idx);
            }
            if (j == NULL)
            {
                printf("ERROR IN FG, No such job\n");
                return (-1);
            }

            if (kill(-j->pgid, SIGCONT) < 0) 
            {
                perror("kill (SIGCONT)");
            }
            // set the job to be the foreground job
            int status;
            tcsetpgrp(STDIN_FILENO, j->pgid);
            currentFG = j;
            j->background = 0;
            waitpid(j->pgid, &status, WUNTRACED);
            
            currentFG = NULL;

            tcsetpgrp(STDIN_FILENO, getpgrp());
        }

        else if (strcmp(args[0], "bg") == 0)
        {
            job *j;
            if (args[1])
            {
                int idx = atoi(args[1]);
                j = find_job(idx);
            }
            else
            {
                int idx = find_greatest_index(0);
                j = find_job(idx);
            }
            if (j == NULL)
            {
                printf("ERROR IN BG, No such job\n");
                return -1;
            }

            if (kill(-j->pgid, SIGCONT) < 0)
            {
                perror("kill (SIGCONT)");
            }
            j->background = 1;
        }

        // Handle built-in commands
        else if (strcmp(args[0], "cd") == 0)
        {
            if (args[1] == NULL)
            {
                fprintf(stderr, "wsh: cd: missing argument\n");
                exit(-1);
            }
            if (args[2] != NULL)
            {
                fprintf(stderr, "Error! cd should not have more than one argument\n");
                exit(-1);
            }
            else
            {
                if (chdir(args[1]) != 0)
                {
                    perror("wsh");
                    exit(-1);
                }
            }
            continue; // Go to the next iteration of the main loop
        }

        else
        {
            //update_jobs();

            // now we can execute the command

            // fork a child process
            child_pid = fork();

            if (child_pid < 0)
            {
                printf("Error forking child process\n");
                exit(-1);
            }

            if (child_pid == 0)
            {
                // child process
                // set the child's PGID to its PID
                setpgid(0, 0);

                // Executing the command using execvp
                if (execvp(args[0], args) < 0)
                {
                    perror("Exec failed");
                    exit(-1);
                }

                // If execvp returns, there was an error
                perror("wsh");
                exit(-1);
            }
            else
            {
                // parent process
                job *new_job = malloc(sizeof(job));
                new_job->pgid = child_pid;
                new_job->command = saved_line;
                new_job->background = background;
                new_job->index = nextSmallestIndex();
                // add the signle process to the jobs list
                process *new_process = malloc(sizeof(process));
                new_process->pid = child_pid;
                new_process->argv = args;
                new_process->next = NULL;
                new_job->first_process = new_process;

                // add the job to the list of jobs

                if (!background)
                {
                    // wait for child process to finish
                    int status;
                    // set the new process to be the forground process group
                    tcsetpgrp(STDIN_FILENO, child_pid);
                    currentFG = new_job;
                    waitpid(child_pid, &status, WUNTRACED);  // suspends execution of the calling process (the shell) until the child process 
               

                    if (WIFSTOPPED(status)) // checks if the child process was stopped by a signal (like SIGTSTP from Ctrl-Z).
                    {

                        currentFG->background = 1;
                        currentFG->index = nextSmallestIndex();

                        if (first_job == NULL)
                        {
                            first_job = currentFG;
                        }
                        else
                        {
                            job *lastJob = first_job;
                            while (lastJob->next != NULL)
                            {
                                lastJob = lastJob->next;
                            }
                            lastJob->next = currentFG;
                        }
                    }

                    currentFG = NULL;
                    tcsetpgrp(STDIN_FILENO, getpgrp());
                    // printf("Child process exited with status %d\n", status);
                }
                else
                {
                    new_job->next = NULL;
                    if (first_job == NULL)
                    {
                        first_job = new_job;
                    }
                    else
                    {
                        job *lastJob = first_job;
                        while (lastJob->next != NULL)
                        {
                            lastJob = lastJob->next;
                        }
                        lastJob->next = new_job;
                    }
                }
            }

            /*
            In either mode, if you hit the end-of-file marker (EOF), you should call exit(0) and exit gracefully. EOF can be generated by pressing Ctrl-D.
            */
        }
    }
}
