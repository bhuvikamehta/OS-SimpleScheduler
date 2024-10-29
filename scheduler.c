#include <stdio.h>
#include <stdlib.h> //include
#include <string.h> 
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <semaphore.h> // Include semaphore library
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>

#define MAXIMUM_PROCESSES 100
#define MAXIMUM_QUEUE_SIZE 100          // Maximum size for process queues

#define MAXIMUM_TERMINATED_PROCESS 100 // Maximum number of terminated processes
#define MAX_PROGRAM_NAMES 256
#define MAXIMUM_CPUS 10                 // Maximum number of CPUs available

// Define some global variables
int NCPU;
int TSLICE;
int share_memoryid; // Shared memory ID

sem_t sem_sch; // Semaphore for scheduling
sem_t sem_output;
sem_t sem_sch_queue; // Semaphore for printing

struct job_queue* ready_queue;
struct job_queue input_queue;
struct completed_queue* finished_queue;

// Struct to represent a process
struct Process {
    pid_t process_id;
    char program_name[MAX_PROGRAM_NAMES];
    int process_state; // 0: Running, 1: Waiting, -1: Finished
    struct timeval starting_time; // Track start time
    struct timeval ending_time;   // Track end time
    long long exec_time;
    long long wait_time;
    int process_priority;
};

// Struct to represent a queue of processes
struct job_queue {
    struct Process processes[MAXIMUM_PROCESSES];
    int rear;
};


// // Struct for terminated processes
// struct completed_queue {
//     struct Process processes[MAXIMUM_PROCESSES];
//     int rear;
// };


// Function to enqueue a process in the queue
void enqueue(struct job_queue* queue, struct Process process) {
    if (queue->rear == MAXIMUM_PROCESSES - 1) {
        printf("maximum limit reached");
        printf("\n");
        
        return;
    }
    //queue->processes[queue->rear] = process;
    struct Process* currentProcess = &queue->processes[queue->rear];
    *currentProcess = process; 
}

// Struct for terminated processes
struct completed_queue {
    struct Process processes[MAXIMUM_PROCESSES];
    int rear;
};


// // Signal handler for SIGUSR1 - Used to wake up the scheduler
// void handleSIGUSR1(int signo) { // Signal the scheduler to wake up
//     sem_post(&sem_sch);
// }

// Function to print the terminated process queue
void printCompletedQueue(struct completed_queue* queue) {
    int i = 0; 
    while (i <= queue->rear) {
        int wait_time = ((queue->processes[i].ending_time.tv_sec - queue->processes[i].starting_time.tv_sec) * 1000) + 
                         ((queue->processes[i].ending_time.tv_usec - queue->processes[i].starting_time.tv_usec) / 1000);
        
        printf("Terminated process %s with PID: %d \n Execution Time: %lld ms \n Waiting Time: %lld \n",
               queue->processes[i].program_name, queue->processes[i].process_id, 
               queue->processes[i].exec_time, queue->processes[i].wait_time);
        
        i++; 
    }
}


// Signal handler for SIGUSR1 - Used to wake up the scheduler
void handleSIGUSR1(int signo) { // Signal the scheduler to wake up
    sem_post(&sem_sch);
}

static void manage_SIGINT(int sig_id){
    printf("\n");
    if (ready_queue == NULL || ready_queue->rear == 0) {
        printf("No processes to display.\n");
        exit(0); 
    }
    printCompletedQueue(finished_queue);
    exit(0); //exits after showing detials 
}
// Signal handler for child process completion SIGCHILD
void handleSIGCHLD(int signo) {
    int status;
    pid_t process_id;

    while ((process_id = waitpid(-1, &status, WNOHANG)) > 0) {
        sem_wait(&sem_sch_queue); // Lock the ready_queue

        // Find the process in the scheduling queue
        int i = 0; 
        while (i <= ready_queue->rear) {
            if (ready_queue->processes[i].process_id == process_id) {
                ready_queue->processes[i].process_state = -1; // Set process_state to finished
                gettimeofday(&ready_queue->processes[i].ending_time, NULL);

                struct timeval duration;

                // duration.tv_sec = ready_queue->processes[i].ending_time.tv_sec - ready_queue->processes[i].starting_time.tv_sec;
                long endingSeconds = ready_queue->processes[i].ending_time.tv_sec;
                long startingSeconds = ready_queue->processes[i].starting_time.tv_sec;
                duration.tv_sec = endingSeconds - startingSeconds;
                // duration.tv_usec = ready_queue->processes[i].ending_time.tv_usec - ready_queue->processes[i].starting_time.tv_usec;
                long endingMicroseconds = ready_queue->processes[i].ending_time.tv_usec;
                long startingMicroseconds = ready_queue->processes[i].starting_time.tv_usec;
                duration.tv_usec = endingMicroseconds - startingMicroseconds;

                long long elapsed = (duration.tv_sec * 1000) + (duration.tv_usec / 1000);
                // ready_queue->processes[i].exec_time += elapsed;
                // ready_queue->processes[i].wait_time += elapsed;

                ready_queue->processes[i].exec_time = ready_queue->processes[i].exec_time + elapsed;
                ready_queue->processes[i].wait_time = ready_queue->processes[i].wait_time + elapsed;


                // Move the process to the terminated queue
                finished_queue->rear++;
                finished_queue->processes[finished_queue->rear] = ready_queue->processes[i];

                // Remove the process from the scheduling queue by shifting elements
                for (int j = i; j < ready_queue->rear; j++) {
                    ready_queue->processes[j] = ready_queue->processes[j + 1];
                }
                ready_queue->rear--;
                break;
            }
            i++; 
        }
        
        sem_post(&sem_sch_queue); // Unlock the ready_queue
    }
}


int get_pid_by_name(const char *name) {
    DIR *proc = opendir("/proc");
    if (proc == NULL) {
        perror("opendir failed");
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_type == DT_DIR && atoi(entry->d_name) > 0) { 
            char path[PATH_MAX];  // Check if it's a PID directory
            char  comm[256];
            snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
            
            FILE *fp = fopen(path, "r");
            if (fp) {
                fgets(comm, sizeof(comm), fp);
                fclose(fp);

                // Remove trailing newline from comm
                comm[strcspn(comm, "\n")] = '\0';

                if (strcmp(name, comm) == 0) {
                    closedir(proc);
                    return atoi(entry->d_name); // Return PID as an integer
                }
            }
        }
    }

    closedir(proc);
    return -1; // Process not found
}


int main() {
    // Initialize semaphores
    if (sem_init(&sem_sch, 0, 0) == -1) {
        // perror("sem_init (sem_sch)");
        // exit(1);
        printf("failed to initialize sem_sch");
        printf("\n");
        exit(1);
    }
    if (sem_init(& sem_output, 0, 1) == -1) {
        // perror("sem_init ( sem_output)");
        // exit(1);
        printf("failed to initialize sem_output");
        printf("\n");
        exit(1);
    }
    if (sem_init(&sem_sch_queue, 0, 1) == -1) {
        // perror("sem_init (sem_sch_queue)");
        // exit(1);
        perror("failed to initialize sem_sch_queue");
        printf("\n");
        exit(1);
    }
    // Setting the function for SIGINT (Ctrl + C)
    if (signal(SIGINT, manage_SIGINT) == SIG_ERR){
        //printf("failed to setup SIGINT handler");
        printf("error setting up SIGINT handler");
        printf("\n");
    }

    // Set up the SIGCHLD signal handler
    if (signal(SIGCHLD, handleSIGCHLD) == SIG_ERR) {
        // printf("failed to setup SIGCHILD handler");        
        // exit(1);
        printf("error setting up SIGCHILD handler");
        printf("\n");
        exit(1);
    }

    printf("Number of CPUs: ");
    scanf("%d", &NCPU);
    printf("Enter TSLICE (ms): ");
    scanf("%d", &TSLICE);

    // Register the SIGUSR1 signal handler
    if (signal(SIGUSR1, (void (*)(int)) handleSIGUSR1) == SIG_ERR) {
        printf("Error setting up SIGUSR1 handler");
        printf("\n");
        exit(1);
    }

    // Create shared memory for the process queue
    share_memoryid = shmget(IPC_PRIVATE, sizeof(struct job_queue), 0666 | IPC_CREAT);
    if (share_memoryid < 0) {
        printf("Error creating shared memory with shmget");
        printf("\n");
        exit(1);
    }

    ready_queue = shmat(share_memoryid, NULL, 0);

    if (ready_queue == (void*) -1) {
        printf("Error attaching shared memory with shmat");
        printf("\n");
        exit(1);
    }
    ready_queue->rear = -1;

    // Create shared memory for the terminated queue
    int terminated_shmid = shmget(IPC_PRIVATE, sizeof(struct completed_queue), 0666 | IPC_CREAT);
    if (terminated_shmid < 0) {
        printf("Error creating shared memory for terminated queue with shmget");
        printf("\n");
        exit(1);
    }

    finished_queue = shmat(terminated_shmid, NULL, 0);

    if (finished_queue == (void*) -1) {
        perror("shmat for terminated queue");
        exit(1);
    }
    finished_queue->rear = -1;

    // Fork the SimpleScheduler process
    pid_t scheduler_pid = fork();
    if (scheduler_pid == -1) {
        printf("error in the SimpleScheduler process");
        printf("\n");
        exit(1);
    }
    else if (scheduler_pid == 0) {
        // Child process (SimpleScheduler)
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = TSLICE * 1000000; // TSLICE in microseconds
        int x = 0;

        // printf("Scheduler process is starting.\n
        while (true) {
            // printf("Scheduler process is waiting for a signal.\n");
            // sem_wait(&sem_sch); // Wait for a process to schedule
            // printf("Scheduler process received a signal.\n");

            if (x > ready_queue->rear || x >= MAXIMUM_PROCESSES) {
                x = 0;
            }
            x = 0;
            // Check if processes are waiting
            int processesToStart = NCPU;
            int flag = 0;

            int i = x; 
            while (i <= ready_queue->rear && processesToStart > 0){
                if (ready_queue->processes[i].process_state == 1 && 
                ready_queue->processes[i].process_priority == 1 && (flag == 0 || flag == 1)){
                    if (sem_wait(&sem_output) == -1){
                        printf("sem_wait failed: %s\n", strerror(errno));
                        printf("\n");
                    }

                    if (sem_post(&sem_output) == -1) {
                        printf("sem_post failed: %s\n", strerror(errno));
                        printf("\n");
                    }
                    gettimeofday(&ready_queue->processes[i].starting_time, NULL);
                    kill(ready_queue->processes[i].process_id, SIGCONT);
                    ready_queue->processes[i].process_state = 0; // Set process_state to running
                    processesToStart--;
                    flag = 1;
                    x = i;

                }
                i++;
            }

            int j = x;

            while (j <= ready_queue->rear && processesToStart > 0){
                if (ready_queue->processes[j].process_state == 1 && 
                ready_queue->processes[j].process_priority == 2 && (flag == 0 || flag == 2)){
                    // Start a waiting process
                    if (sem_wait(&sem_output) == -1) {
                        printf("sem_wait failed: %s\n", strerror(errno));
                        printf("\n");
                    }
                    if (sem_post(&sem_output) == -1){
                        printf("sem_post failed: %s\n", strerror(errno));
                        printf("\n");
                    }
                    gettimeofday(&ready_queue->processes[j].starting_time, NULL);
                    kill(ready_queue->processes[j].process_id, SIGCONT);
                    ready_queue->processes[j].process_state = 0; // Set process_state to running
                    processesToStart--;
                    flag = 2;
                    x = j;
                }
                j++;
            }
        

            int k = x;
            while (k <= ready_queue->rear && processesToStart > 0){
                if (ready_queue->processes[k].process_state == 1 && 
                ready_queue->processes[k].process_priority == 3 && (flag == 0 || flag == 3)){

                    if (sem_wait(&sem_output) == -1) {
                        printf("sem_wait failed: %s\n", strerror(errno));
                        printf("\n");
                    }
                    if (sem_post(&sem_output) == -1) {
                        perror("sem_post (sem_output)");
                    }
                    gettimeofday(&ready_queue->processes[k].starting_time, NULL);
                    kill(ready_queue->processes[k].process_id, SIGCONT);
                    ready_queue->processes[k].process_state = 0; // Set process_state to running
                    processesToStart--;
                    flag = 3;
                    x = k;
                }
                k++; 
            }

        
            int l = x;
            while (l <= ready_queue->rear && processesToStart > 0){
                if (ready_queue->processes[l].process_state == 1 && 
                ready_queue->processes[l].process_priority == 4 && (flag == 0 || flag == 4)){
                    if (sem_wait(&sem_output) == -1) {
                        perror("sem_wait (sem_output)");
                    }
                
                    if (sem_post(&sem_output) == -1) {
                        perror("sem_post (sem_output)");
                    }

                    gettimeofday(&ready_queue->processes[l].starting_time, NULL);
                    kill(ready_queue->processes[l].process_id, SIGCONT);
                    ready_queue->processes[l].process_state = 0; // Set process_state to running
                    processesToStart--;
                    flag = 4;
                    x = l;
                }
                l++;
            }
           
            // Sleep for TSLICE
            int t = TSLICE;
            if (flag != 0){
                t = TSLICE/flag; //Scheduler process going for sleeping
            }
            usleep(t * 1000);
            int y = 0;

            while (y <= ready_queue->rear){

                if (ready_queue->processes[y].process_state == 0){
                    if (sem_wait(&sem_output) == -1) {
                        printf("sem_wait failed: %s\n", strerror(errno));
                        printf("\n");
                    }

                    if (sem_post(&sem_output) == -1) {
                        printf("sem_post failed: %s\n", strerror(errno));
                        printf("\n");
                    }

                    if (kill(ready_queue->processes[y].process_id, SIGSTOP) == -1) {
                        printf("kill (SIGSTOP) failed: %s\n", strerror(errno));
                        printf("\n");
                    }
                    gettimeofday(&ready_queue->processes[y].ending_time, NULL);

                    long endingSeconds = ready_queue->processes[y].ending_time.tv_sec;
                    long startingSeconds = ready_queue->processes[y].starting_time.tv_sec;
                    long durationSeconds = endingSeconds - startingSeconds;

                    long endingMicroseconds = ready_queue->processes[y].ending_time.tv_usec;
                    long startingMicroseconds = ready_queue->processes[y].starting_time.tv_usec;
                    long durationMicroseconds = endingMicroseconds - startingMicroseconds;

                    long long elapsed = (durationSeconds * 1000) + (durationMicroseconds / 1000);

                    ready_queue->processes[y].exec_time += elapsed;
                    ready_queue->processes[y].wait_time += (ready_queue->rear - 1) * TSLICE;
                    ready_queue->processes[y].process_state = 1; // Set process_state to waiting
                }
                y++;
            }
        }
    } 

    else {
        // Parent process (SimpleShell)
        input_queue.rear = -1;

        while (true) {
            char program_name[MAX_PROGRAM_NAMES];
            printf("\nSimpleShell>>>");
            scanf("%s", program_name);

            if (strcmp(program_name, "exit") == 0) {
                // Exit the shell
                break;
            } else if (strcmp(program_name, "submit") == 0) {
                // User submits a program
                char program[MAX_PROGRAM_NAMES];
                int prior = 1; // Default process_priority is 1
                // Try to read the program name
                scanf("%s", program);
           
                // Try to read the process_priority if available
                if (scanf("%d", &prior) != 1) {
                    // Priority was not entered, so it remains as 1
                    prior = 1;
                }
                if (prior < 1 || prior > 4) {
                    printf("priority must be between 1 and 4");
                    printf("\n");
                    prior = 1;
                    continue;
                }

                pid_t child_pid = fork();
                if (child_pid == -1) {
                    perror("fork");
                    continue;
                }
                // int process_id=atoi(program);
                int piD = get_pid_by_name(program);
                if (child_pid == 0) {
                    // Child process
                    usleep(TSLICE * 1000);
                    execlp(program, program, NULL);
                    kill(piD,SIGSTOP);
                    perror("Execution failed");
                    exit(1);
                } else {
                    // Parent process (shell)
                    struct Process new_process;
                    new_process.process_id = child_pid;
                    strcpy(new_process.program_name, program);
                    new_process.process_priority=prior;
                    new_process.process_state = 1; // Set process_state to waiting
                    new_process.exec_time = 0; // Set initial execution time = 0
                    new_process.wait_time = TSLICE; // Initialize waiting time to 0
                    if (ready_queue->rear < MAXIMUM_PROCESSES - 1) {
                        ready_queue->rear++;
                        enqueue(ready_queue, new_process);
                        kill(new_process.process_id,SIGSTOP);

                        // Send SIGUSR1 to the scheduler to wake it up
                        if (kill(scheduler_pid, SIGUSR1) == -1) {
                            perror("kill (SIGUSR1)");
                        }
                    } else {
                        printf("Scheduler queue is full. Cannot submit more processes.\n");
                    }
                }
            } else {
                // Execute other commands or system commands
                if (system(program_name) == -1) {
                    perror("system");
                }
            }
        }

        // Wait for child processes to complete
        while (input_queue.rear >= 0) {
            int status;
            pid_t process_id = wait(&status);
            if (process_id == -1) {
                // No more child processes to wait for
                break;
            }
            for (int i = 0; i <= input_queue.rear; i++) {
                if (input_queue.processes[i].process_id == process_id) {
                    if (sem_wait(& sem_output) == -1) {
                        perror("sem_wait ( sem_output)");
                    }
                    printf("Process with PID %d finished execution. Execution Time: %lld ms\n", process_id, input_queue.processes[i].exec_time);
                    if (sem_post(& sem_output) == -1) {
                        perror("sem_post ( sem_output)");
                    }
                    input_queue.processes[i].process_state = -1; // Set process_state to finished
                    gettimeofday(&input_queue.processes[i].ending_time, NULL);
                    struct timeval duration;
                    duration.tv_sec = input_queue.processes[i].ending_time.tv_sec - input_queue.processes[i].starting_time.tv_sec;
                    duration.tv_usec = input_queue.processes[i].ending_time.tv_usec - input_queue.processes[i].starting_time.tv_usec;
                    long long elapsed = duration.tv_sec * 1000 + duration.tv_usec / 1000;
                    input_queue.processes[i].exec_time += elapsed;
                   
                }
            }
        }
        printCompletedQueue(finished_queue);
        exit(0);
    }
    printCompletedQueue(finished_queue);
    // Clean up shared memory
    if (shmdt(ready_queue) == -1) {
        perror("shmdt (ready_queue)");
    }
    if (shmctl(share_memoryid, IPC_RMID, NULL) == -1) {
        perror("shmctl (ready_queue)");
    }

    // Destroy semaphores
    if (sem_destroy(&sem_sch) == -1) {
        perror("sem_destroy (sem_sch)");
    }
    if (sem_destroy(& sem_output) == -1) {
        perror("sem_destroy ( sem_output)");
    }
    if (sem_destroy(&sem_sch_queue) == -1) {
        perror("sem_destroy (sem_sch_queue)");
    }
    exit(0);
    return 0;
}
