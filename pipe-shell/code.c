#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGS 100
#define HISTORY_COUNT 100

// Struct to store command execution details
typedef struct {
    char command[MAX_COMMAND_LENGTH];
    pid_t pid;                 // Process ID of the command
    struct timeval startTime; // Start time of the command
    struct timeval end_time;   // End time of the command
} CommandDetails;

// Struct to store command history
typedef struct {
    CommandDetails commands[HISTORY_COUNT];
    int count;
} History;

History history;

// Function to handle errors and print error message
void handle_error(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

// Function to print the command prompt
void printPompt() {
    printf("UNIX_Shell> ");
}

// Function to parse input into command and arguments
void PARSE(char *input, char **args) {
    char *_input = strtok(input, " \t\r\n");
    int i = 0;
    while (_input != NULL) {
        args[i++] = _input;
        _input = strtok(NULL, " \t\r\n");
    }
    args[i] = NULL;
}

// Function to log command execution details
void logDetails(pid_t pid, struct timeval start, struct timeval end) {
    if (history.count == 0) return;      // nothing to log
    CommandDetails *cmdDetails = &history.commands[history.count - 1];
    cmdDetails->pid = pid;
    cmdDetails->startTime = start;
    cmdDetails->end_time = end;
}

// Function to display command execution details when the shell terminates
void display_execution_details() {
    printf("\nExecution Details:\n");
    for (int i = 0; i < history.count; i++) {
        CommandDetails cmd = history.commands[i];
        long seconds = cmd.end_time.tv_sec - cmd.startTime.tv_sec;
        long microseconds = cmd.end_time.tv_usec - cmd.startTime.tv_usec;
        double elapsed = seconds + microseconds * 1e-6;
        printf("Command: %s\n", cmd.command);
        printf("PID: %d\n", cmd.pid);
        printf("Start Time: %ld.%06ld\n", (long)cmd.startTime.tv_sec, (long)cmd.startTime.tv_usec);
        printf("End Time: %ld.%06ld\n", (long)cmd.end_time.tv_sec, (long)cmd.end_time.tv_usec);
        printf("Duration: %.6f seconds\n", elapsed);
        printf("-------------------------------------------------\n");
    }
}

// Function to add a command to history 
void addHistory(const char *input) {
    if (history.count < HISTORY_COUNT) {
        // store EXACT user input before any splitting/parsing
        strcpy(history.commands[history.count].command, input);
        // initializing the placeholders 
        history.commands[history.count].pid = -1;
        history.commands[history.count].startTime.tv_sec = 0;
        history.commands[history.count].startTime.tv_usec = 0;
        history.commands[history.count].end_time.tv_sec = 0;
        history.commands[history.count].end_time.tv_usec = 0;
        history.count++;                 
    }
}

// Function to display History
void show_history() {
    for (int i = 0; i < history.count; i++) {
        printf("%d: %s\n", i + 1, history.commands[i].command);
    }
    printf("\n");
}

// Function to execute a single (non-piped) command
void executeCommand(char **args) {
    struct timeval start, end;
    gettimeofday(&start, NULL);

    pid_t pid = fork();
    if (pid < 0) {
        handle_error("Fork failed");
    } else if (pid == 0) {               // child
        execvp(args[0], args);
        handle_error("Execution failed"); // only reached on error
        _exit(EXIT_FAILURE);
    } else {                             // parent
        waitpid(pid, NULL, 0);
        gettimeofday(&end, NULL);        
        logDetails(pid, start, end);
    }
}

// Function to execute piped commands
void executePipedCommands(char *cmds[], int num_cmds) {
    int pipefds[2 * (num_cmds - 1)];
    for (int i = 0; i < num_cmds - 1; i++) {
        if (pipe(pipefds + 2*i) < 0) handle_error("Pipe failed");
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    pid_t last_pid = -1;

    for (int i = 0; i < num_cmds; i++) {
        pid_t pid = fork();
        if (pid < 0) handle_error("Fork failed");

        if (pid == 0) {
            // child i
            if (i > 0) { // connecting the stdin to previous pipe read end
                if (dup2(pipefds[2*(i-1)], STDIN_FILENO) < 0) handle_error("dup2 input failed");
            }
            if (i < num_cmds - 1) { // connect stdout to current pipe write end
                if (dup2(pipefds[2*i + 1], STDOUT_FILENO) < 0) handle_error("dup2 output failed");
            }
            // close all pipe fds of the child
            for (int j = 0; j < 2*(num_cmds - 1); j++) close(pipefds[j]);

            // parsing this stage and exec call 
            char *args[MAX_ARGS];
            PARSE(cmds[i], args);
            execvp(args[0], args);
            handle_error("execvp failed");
            _exit(EXIT_FAILURE);
        } else {
            if (i == num_cmds - 1) last_pid = pid; 
        }
    }

    // closing all pipe fds of parent
    for (int j = 0; j < 2*(num_cmds - 1); j++) close(pipefds[j]);

    //waiting for all the child processes
    for (int i = 0; i < num_cmds; i++) wait(NULL);

    gettimeofday(&end, NULL);            
    logDetails(last_pid, start, end);
}

// Ctrl+C handler
void my_handler() {
    display_execution_details();
    exit(0);
}

int main()
{
    struct sigaction sig;
    memset(&sig, 0, sizeof(sig));
    sig.sa_handler = my_handler;

    
    if (sigaction(SIGINT, &sig, NULL) == -1)
    {
        handle_error("Error installing signal handler");
    }

    char input[MAX_COMMAND_LENGTH];

    while (1)
    {
        printPompt();

        if (fgets(input, MAX_COMMAND_LENGTH, stdin) == NULL)
        {
            handle_error("Failed to read input");
        }

        // Removing the newline character
        input[strcspn(input, "\n")] = 0;

        //condition to exit
        if (strcmp(input, "exit") == 0)
        {
            break;
        }

        // Check if the user is giving the history command
        if (strcmp(input, "history") == 0) {
            show_history();
            continue;
        }

        
        addHistory(input);

        // Check if the input has a pipe
        if (strstr(input, "|") != NULL)
        {
            
            char *cmds[MAX_ARGS];
            int num_cmds = 0;
            cmds[num_cmds] = strtok(input, "|");
            while (cmds[num_cmds] != NULL)
            {
                num_cmds++;
                cmds[num_cmds] = strtok(NULL, "|");
            }

            
            executePipedCommands(cmds, num_cmds);
        }
        else
        {
            // Handling a simple command
            char *args[MAX_ARGS];
            PARSE(input, args);

           
            executeCommand(args);
        }
    }
    return 0;
}