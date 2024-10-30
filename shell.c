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

#define MAXIMUM_COMMANDS 256
#define MSGQ_KEY 12345

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
        //printf("\nReceived SIGINT, shutting down...\n");
        exit_flag = 1;
        if (scheduler_processid > 0) {
            kill(scheduler_processid, SIGTERM);
        }
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

char* get_scheduler_path() {
    static char path[MAXIMUM_COMMANDS];
    char *dir = dirname(realpath("/proc/self/exe", NULL));
    snprintf(path, sizeof(path), "%s/scheduler", dir);
    return path;
}

void launch_scheduler(int NCPU, int TSLICE) {
    char *scheduler_path = get_scheduler_path();
    
    // Check if scheduler exists
    if (access(scheduler_path, X_OK) != 0) {
        printf("Error: Cannot find scheduler executable at %s\n", scheduler_path);
        printf("Error details: %s\n", strerror(errno));
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        char ncpu_str[10], tslice_str[10];
        sprintf(ncpu_str, "%d", NCPU);
        sprintf(tslice_str, "%d", TSLICE);
        
        if (execl(scheduler_path, scheduler_path, ncpu_str, tslice_str, NULL) == -1) {
            printf("Error executing scheduler: %s\n", strerror(errno));
            exit(1);
        }
    } else if (pid > 0) {
        // Parent process
        scheduler_processid = pid;
        usleep(100000); // 100ms delay for initialization
        
        // Verify scheduler is running
        if (kill(scheduler_processid, 0) == -1) {
            printf("Error: Scheduler failed to start\n");
            exit(1);
        }
    } else {
        // Fork failed
        perror("Fork failed");
        exit(1);
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

    // Create message queue
    message_queueid = msgget(MSGQ_KEY, 0666 | IPC_CREAT);
    if (message_queueid == -1) {
        perror("msgget failed");
        exit(1);
    }

    launch_scheduler(NCPU, TSLICE);
   // printf("Scheduler launched with PID: %d\n", scheduler_processid);
   // printf("Commands:\n");
    // printf(" submit <program> [priority] - Submit a program\n");
    // printf(" run - Start executing submitted programs\n");
    // printf(" exit - Exit the shell\n");

    while (!exit_flag) {
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
            char *cmd = strtok(input, " ");
            if (cmd && strcmp(cmd, "submit") == 0) {
                char *program = strtok(NULL, " ");
                char *priority_str = strtok(NULL, " ");

                if (program) {
                    char abs_path[MAXIMUM_COMMANDS];
                    if (program[0] != '/') {  // If not absolute path
                        char cwd[MAXIMUM_COMMANDS];
                        if (getcwd(cwd, sizeof(cwd)) != NULL) {
                            int result = snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, program);
                            if (result >= sizeof(abs_path)) {
                                printf("Error: Path too long\n");
                                continue;
                            }
                        } else {
                            perror("getcwd failed");
                            continue;
                        }  
                    } else {
                        size_t len = strlen(program);
                        if (len >= sizeof(abs_path)) {
                            printf("Error: Path too long\n");
                            continue;
                        }
                        strncpy(abs_path, program, sizeof(abs_path)-1);
                        abs_path[sizeof(abs_path)-1] = '\0';
                    }

                    if (access(abs_path, X_OK) == 0) {
                        message.message_type = 1;
                        strncpy(message.command, abs_path, MAXIMUM_COMMANDS-1);
                        message.priority = (priority_str != NULL) ? atoi(priority_str) : 1;

                        if (msgsnd(message_queueid, &message, sizeof(message)-sizeof(long), 0) == -1) {
                            perror("msgsnd failed");
                        } 
                        else {
                            printf("Submitted: %s with priority %d\n", program, message.priority);
                        }
                    } 
                    else {
                        printf("Error: %s is not executable or doesn't exist\n", program);
                        printf("Error details: %s\n", strerror(errno));
                    }
                } 
                else {
                   // printf("Usage: submit <program> [priority]\n");
                }
            } else if (strlen(input) > 0) {
                printf("Invalid command");
                printf("\n");
            }
        }
    }

    //printf("\nShutting down...\n");
    free_resource();
    return 0;
}
