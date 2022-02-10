/*
Assignment 3 - CS344
Timothy Yehan Lee
Prof. Lewis
Winter 2022
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

volatile sig_atomic_t no_background_flag = 0;

typedef struct userInput userInput;         // type alias for struct
struct userInput {
    char *command;
    char *input_file;
    char *output_file;
    int background;
    char *input_args[512];
};

typedef struct execCommandReturn execCommandReturn;
struct execCommandReturn {
    int last_exit_status;
    pid_t child_pid;
};

void handle_SIGTSTP_exit(int signal_num);

execCommandReturn* executeCommand(userInput* _userInput) {
    // execute using fork(), exec() and waitpid()

    // fork off child
    struct execCommandReturn* cmdReturn = malloc(sizeof(execCommandReturn));
    int childStatus;
    pid_t childPid = -5;
    
    int outputFD;
    int inputFD;

    childPid = fork();

    switch (childPid) {
        case -1:
            // fork failed
            perror("fork() failed!\n");
            exit(1);
            break;
        case 0: ;       // semicolon to avoid "a label can only be part of a statement and a declaration is not a statement" error

            // both foreground and background child ignore SIGTSTP
            struct sigaction SIGTSTP_action = {{0}};
            SIGTSTP_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &SIGTSTP_action, NULL);

            // check for output redirect - written with help from Canvas
            if (_userInput->output_file) {
                outputFD = open(_userInput->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (outputFD == -1) { perror("open()"); }

                // redirect with dup2
                int dupResult = dup2(outputFD, STDOUT_FILENO);
                if (dupResult == -1) { perror("dup2"); }
            }
            // check for input redirect
            if (_userInput->input_file) {
                inputFD = open(_userInput->input_file, O_RDONLY);
                if (inputFD == -1) { perror("input open()"); }

                int dupResult = dup2(inputFD, STDIN_FILENO);
                if (dupResult == -1) { perror("dup2"); }
            }
            // child will execute code in this branch - use exec() to run command
            // execvp uses PATH variable to look for command, and allow shell scripts to be executed
            if ((_userInput->background) && (no_background_flag == 0)) {
                // if user doesn't redirect input for background command, standard input should redirect to /dev/null
                if (!_userInput->input_file) {
                    inputFD = open("/dev/null", O_RDONLY);
                    if (inputFD == -1) { perror("input open()"); }
                    
                    int dupResult = dup2(inputFD, STDIN_FILENO);
                    if (dupResult == -1) { perror("dup2"); }
                }

                // if user doesn't redirect output, standard output should redirect to /dev/null
                if (!_userInput->output_file) {
                    outputFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
                    if (outputFD == -1) { perror("open()"); }

                    // redirect with dup2
                    int dupResult = dup2(outputFD, STDOUT_FILENO);
                    if (dupResult == -1) { perror("dup2"); }
                }

                execvp(_userInput->command, _userInput->input_args);
            } else {
                // foreground task
                
                // foreground child process should have default handle to SIG_INT
                struct sigaction SIGINT_action = {{0}};
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);

                execvp(_userInput->command, _userInput->input_args);
            }
            // exec only returns if there is an error
            exit(2);
            break;
        default:
            if ((_userInput->background) && (no_background_flag == 0)) {
                printf("background pid is: %d\n", childPid);
                fflush(stdout);

                cmdReturn->child_pid = childPid;      // only record status for background tasks
            }

            // parent will execute code in this branch
            if ((no_background_flag == 1) || (!_userInput->background)) {
                // only wait for foreground tasks
                waitpid(childPid, &childStatus, 0);
                
                if (WIFSIGNALED(childStatus)) {
                    // if foreground task is killed by signal, print out number of signal that killed child
                    printf("foreground task killed by signal: %i\n", WTERMSIG(childStatus));
                    fflush(stdout);
                }

                cmdReturn->last_exit_status = childStatus;
            }

            if(WEXITSTATUS(childStatus) == 2){
                // if cannot find command to run, then shell prints error message and sets exit status to 1
                perror("shell couldn't find command!\n");
                cmdReturn->last_exit_status = 1;
            }

            return cmdReturn;
    }
}

void checkBackgroundTerminates(int processCount) {
    // when bg process terminates, print the process id and exit status - must be printed just before the prompt for new command
    int childStatus;
    pid_t terminated_pid;

    do {
        terminated_pid = waitpid(-1, &childStatus, WNOHANG);

        if (terminated_pid > 0) {
            printf("background task: %d terminated with exit status: %i\n", terminated_pid, WEXITSTATUS(childStatus));
            fflush(stdout);
            terminated_pid = waitpid(-1, &childStatus, WNOHANG);
        }
    } while (terminated_pid > 0);
}

void exitCommand(int child_count, pid_t* child_pids[]) {
    // exit shell - kill any other processes or jobs that shell has started
    // SIGKILL is a kill signal

    for (int i = 0; i < child_count; ++i) {
        kill(*(child_pids[i]), SIGTERM);
    }
    exit(0);
}

void goHomeDirectory() {
    // no args - change to directory specified in HOME env var
    char* home_dir;

    home_dir = getenv("HOME");     // get home dir
    chdir(home_dir);
}

void changeDirectoryCommand(char* dirPath) {
    // one arg - path to directory, should support both absolute and relative paths
    char cwd[256];
    char* fullRelative;

    // check if "/" is first character
    if (*dirPath == '/') {
        chdir(dirPath);
    } else {
        // relative path
        getcwd(cwd, 256);    // get home directory
        fullRelative = calloc(strlen(dirPath) + strlen(cwd) + 1, sizeof(char));
        sprintf(fullRelative, "%s/%s", cwd, dirPath);
        chdir(fullRelative);
    }
}

void statusCommand(int exit_status) {
    // prints exit status or terminating signal of the last foreground process ran by shell
    // if run before any foreground command is run, then simply return exit status 0
    // three built-in shell commands don't count as foreground for this command
    
    if (WIFEXITED(exit_status)) {
        // normal exit
        printf("exit value: %i\n", exit_status);
        fflush(stdout);
    } else if (WIFSIGNALED(exit_status)) {
        // abnormal termination
        printf("abnormal terminating signal: %d\n", WTERMSIG(exit_status));
        fflush(stdout);
    }
}

char* varExpansion(int len, char* inputToken) {
    // expand variable && to pid of smallsh
    static char* tempToken;
    char process_id[20];
    char* prevToken;
    char* postToken;

    for (int i = 0; i < len; ++i) {

        if ((*(inputToken + i) == *(inputToken + i - 1)) && (*(inputToken + i) == '$')) {
            // if current char and previous char are both "$", found "$$"
            sprintf(process_id, "%d", getpid());
            tempToken = calloc((strlen(inputToken) + strlen(process_id) - 2 + 1), sizeof(char));
            prevToken = calloc(i, sizeof(char));        // copy chars before "$$"
            postToken = calloc(strlen(inputToken) - i, sizeof(char));     // copy chars after "$$"
            
            strncpy(prevToken, inputToken, i - 1);
            strncpy(postToken, inputToken + i + 1, strlen(inputToken) - i);
            sprintf(tempToken, "%s%s%s", prevToken, process_id, postToken);     // recombine strings
            return tempToken;
        }
    }
    static char* returnToken;
    returnToken = inputToken;
    return returnToken;
}

void handle_SIGTSTP_enter(int signal_num) {
    char const message[] = "Entering foreground-only mode (& is now ignored)\n";
    write(STDOUT_FILENO, message, sizeof message - 1);

    no_background_flag = 1;

    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = handle_SIGTSTP_exit;
    sigfillset(&SIGTSTP_action.sa_mask);         // block all catchable signals while handle_SIGTSTP is running
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void handle_SIGTSTP_exit(int signal_num) {
    char const message[] = "Exiting foreground-only mode\n";
    write(STDOUT_FILENO, message, sizeof message - 1);

    no_background_flag = 0;

    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = handle_SIGTSTP_enter;
    sigfillset(&SIGTSTP_action.sa_mask);         // block all catchable signals while handle_SIGTSTP is running
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}

void commandPrompt() {

    char buffer[2050];
    char* saveptr;
    char* expandToken;
    int arg_count = 1;
    pid_t* child_pids[200];
    int child_count = 0;
    int last_exit_status = 0;

    // signal handlers - written with the help of canvas
    struct sigaction SIGINT_action = {{0}};
    SIGINT_action.sa_handler = SIG_IGN;         // ignore constant
    sigaction(SIGINT, &SIGINT_action, NULL);

    struct sigaction SIGTSTP_action = {{0}};
    SIGTSTP_action.sa_handler = handle_SIGTSTP_enter;
    sigfillset(&SIGTSTP_action.sa_mask);         // block all catchable signals while handle_SIGTSTP is running
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    while (1) {
        arg_count = 1;       // reset for every new command
        do {
            // written with help from this post: https://stackoverflow.com/questions/42265038/how-to-check-if-user-enters-blank-line-in-scanf-in-c
            // if command starts with # or is blank line, ignore and reprompt
            printf(": ");
            fflush(stdout);

            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                continue;
            }
        } while((strncmp(buffer, "\n", 2) == 0) || strncmp(buffer, "#", 1) == 0 || (strncmp(buffer, " ", 1) == 0));

        // create userInput struct
        struct userInput *currentInput = malloc(sizeof(userInput));

        // break down input into parts
        char *token = strtok_r(buffer, " \n", &saveptr);
        currentInput->command = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currentInput->command, token);

        // copy command to first arg
        currentInput->input_args[0] = calloc(strlen(token) + 1, sizeof(char));
        strcpy(currentInput->input_args[0], token);

        token = strtok_r(NULL, " \n", &saveptr);
        while(token != NULL){

            if (strcmp(token, "<") == 0) {
                // take next token and save as inputfile
                token = strtok_r(NULL, " \n", &saveptr);
                expandToken = varExpansion(strlen(token), token);
                currentInput->input_file = calloc(strlen(expandToken) + 1, sizeof(char));
                strcpy(currentInput->input_file, expandToken);

            } else if (strcmp(token, ">") == 0) {
                // take next token and save as outputfile
                token = strtok_r(NULL, " \n", &saveptr);
                expandToken = varExpansion(strlen(token), token);
                currentInput->output_file = calloc(strlen(expandToken) + 1, sizeof(char));
                strcpy(currentInput->output_file, expandToken);

            } else if (strcmp(token, "&") == 0) {
                // set background to true
                currentInput->background = 1;

            } else {
                expandToken = varExpansion(strlen(token), token);
                currentInput->input_args[arg_count] = calloc(strlen(expandToken) + 1, sizeof(char));
                strcpy(currentInput->input_args[arg_count], expandToken);
                arg_count++;
            }

            token = strtok_r(NULL, " \n", &saveptr);
        }

        // built in commands
        if (strcmp(currentInput->command, "exit") == 0){
            exitCommand(child_count, child_pids);
        }
        else if(strcmp(currentInput->command, "cd") == 0){
            if (arg_count == 0){
                // go to home 
                goHomeDirectory();
            } else {
                changeDirectoryCommand(currentInput->input_args[0]);
            }
        }
        else if(strcmp(currentInput->command, "status") == 0){
            statusCommand(last_exit_status);
        }
        else {      // not a built in command
            struct execCommandReturn* cmdReturn;
            cmdReturn = executeCommand(currentInput);

            if (cmdReturn->last_exit_status) {
                // if foreground task, record exit status
                last_exit_status = cmdReturn->last_exit_status;
            } else if (cmdReturn->child_pid) {
                // if background task, record child pid
                child_pids[child_count] = &cmdReturn->child_pid;
                child_count++;
                
            }
        }
        // check for terminated background processes before looping
        checkBackgroundTerminates(child_count);
    }
}

int main(int argc, char *argv[]) {
    commandPrompt();
    exit(0);
}