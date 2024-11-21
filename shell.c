#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <libgen.h>
#include <errno.h>

#define MAXIMUM_COMMANDS 512

#define INPUT_BUFFER_SIZE 1024
#define MESSAGE_QUEUEKEY 100

struct message_buffer {
    long message_type;
    char command[MAXIMUM_COMMANDS];
    int priority;
} message;

int message_queueid;
pid_t scheduler_processid;
volatile sig_atomic_t exit_flag = 0;

void signal_handler(int signum) {
    if (signum == SIGINT) {
        exit_flag = 1;
        if (scheduler_processid > 0) {
            kill(scheduler_processid, SIGINT);
            int status;
            waitpid(scheduler_processid, &status, 0);
        }
        if (message_queueid != -1) {
            msgctl(message_queueid, IPC_RMID, NULL);
        }
        exit(0);
    }
}

void free_resource() {
    if (scheduler_processid > 0) {
        kill(scheduler_processid, SIGTERM);
        waitpid(scheduler_processid, NULL, 0);
    }
    if (message_queueid != -1) {
        msgctl(message_queueid, IPC_RMID, NULL);
    }
}

char* retieve_schedular() {
    static char path[MAXIMUM_COMMANDS];
    char *directory = dirname(realpath("/proc/self/exe", NULL));
    snprintf(path, sizeof(path), "%s/scheduler", directory);
    return path;
}

void launch_scheduler(int NCPU, int TSLICE) {
    char *scheduler_path = retieve_schedular();
   
    //Check if scheduler exists
    if (access(scheduler_path, X_OK) != 0) {
        printf("scheduler path not exits");
        printf("\n");
        printf("error: %s\n", strerror(errno));
        exit(1);
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("Error in creating child process");
        exit(1);
    }
    else if (pid == 0) {
        char NCPU_str[10], TSLICE_str[10];
        sprintf(NCPU_str, "%d", NCPU);
        sprintf(TSLICE_str, "%d", TSLICE);
   
        if (execl(scheduler_path, scheduler_path, NCPU_str, TSLICE_str, NULL) == -1) {
            printf("Error executing scheduler:\n");
            printf("%s\n", strerror(errno));
            exit(1);
        }
    }
    else {
   
        scheduler_processid = pid;
        usleep(100000);
   
        if (kill(scheduler_processid, 0) == -1) {           //check scheduler running
            printf("Error in schedular starting");
            printf("\n");
            exit(1);
        }
    }
}

int main() {
    struct sigaction signal_action;
    signal_action.sa_handler = signal_handler;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);

    int NCPU, TSLICE;
    char input[MAXIMUM_COMMANDS];

    printf("Enter NCPU: ");
    if (scanf("%d", &NCPU) != 1) {
        printf("input is invalid");
        printf("\n");
        exit(1);
    }

    printf("Enter TSLICE (ms): ");
    if (scanf("%d", &TSLICE) != 1) {
        printf("input not valid");
        printf("\n");
        exit(1);
    }
    getchar(); // Consume newline

    message_queueid = msgget(MESSAGE_QUEUEKEY, 0666 | IPC_CREAT);
    if (message_queueid == -1) {                //message queue create
        printf("Error in executing msgget");
        exit(1);
    }

    launch_scheduler(NCPU, TSLICE);

    while (exit_flag==0) {
        printf("Simpleshell>>> ");
        fflush(stdout);
       
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "exit") == 0) {
            exit_flag = 1;
        }
        else if (strcmp(input, "execute") == 0) {
            if (scheduler_processid > 0) {
                if (kill(scheduler_processid, SIGUSR1) == 0) {}
                else {
                    printf("Signal can't sent to the Scheduler");
                    printf("Error %s",strerror(errno));
                    printf("\n");
                }
            }
        }
        else {
            char *token = strtok(input, " ");
            if (token && strcmp(token, "submit") == 0) {
                char *program = strtok(NULL, " ");
                char *priority_str = strtok(NULL, " ");

                if (program) {
                    char executable_path[MAXIMUM_COMMANDS];
                    if (program[0] != '/') {  // If not absolute path
                        char current_directory[MAXIMUM_COMMANDS];
                        if (getcwd(current_directory, sizeof(current_directory)) != NULL) {
                            int result = snprintf(executable_path, sizeof(executable_path), "%s/%s", current_directory, program);
                            if (result >= sizeof(executable_path)) {
                                printf("path is too long");
                                printf("\n");
                                continue;
                            }
                        }
                        else {
                            printf("cannot retrieve the current working directory");
                            continue;
                        }  
                    }
                    else {
                        size_t len = strlen(program);
                        if (len >= sizeof(executable_path)) {
                            printf("path is too long");
                            printf("\n");
                            continue;
                        }
                        strncpy(executable_path, program, sizeof(executable_path)-1);
                        executable_path[sizeof(executable_path)-1] = '\0';
                    }

                    if (access(executable_path, X_OK) == 0) {
                        message.message_type = 1;
                        strncpy(message.command, executable_path, MAXIMUM_COMMANDS-1);
                        if (priority_str != NULL){
                            message.priority = atoi(priority_str);
                        }
                        else{
                            message.priority = 1;
                        }

                        if (msgsnd(message_queueid, &message, sizeof(message)-sizeof(long), 0) == -1) {
                            perror("error in msgsnd");
                        }
                        else {
                           printf("Submitted: %s\n", program);
                           printf("with priority %d\n", message.priority);
                        }
                    }
                    else {
                        printf("Error: %s is not executable or doesn't exist\n", program);
                        printf("Error details: %s\n", strerror(errno));
                    }
                }
            }
            else if (strlen(input) > 0) {
                printf("Invalid command");
                printf("\n");
            }
        }
    }

    free_resource();
    return 0;
}
