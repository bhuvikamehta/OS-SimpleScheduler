#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/time.h>

#define MAX_PROCESSES 100
#define MAXIMUM_COMMANDS 256
#define MSGQ_KEY 12345

// Process states
#define STATE_CREATED 0
#define STATE_READY 1
#define STATE_RUNNING 2
#define STATE_FINISHED 3

// Message structure for IPC
struct message_buffer {
    long message_type;
    char command[MAXIMUM_COMMANDS];
    int priority;
} message;

// PCB structure
typedef struct {
    pid_t pid;
    char name[MAXIMUM_COMMANDS];
    int state;
    time_t start_time;
    time_t total_exec_time;
    time_t total_wait_time;
    int priority;
    int is_running;
} PCB;

// Ready Queue implementation with priority
typedef struct {
    PCB *processes[MAX_PROCESSES];
    int count;
} ReadyQueue;

ReadyQueue ready_queue;
int msgq_id;
int NCPU;
int TSLICE;
int total_processes = 0;
PCB *process_table[MAX_PROCESSES];
volatile sig_atomic_t should_exit = 0;
volatile sig_atomic_t start_execution = 0;


// Global variables for tracking process state
typedef struct {
    int state;
    struct timeval start_time;
    struct timeval end_time;
    long long total_execution_time;
    long long waiting_time;
    pid_t pid;
    char name[256];
    int priority;
} ProcessInfo;

typedef struct {
    ProcessInfo processes[MAX_PROCESSES];
    int rear;
} ProcessQueue;

ProcessQueue *scheduler_queue;
ProcessQueue *terminated_queue;
sem_t scheduler_queue_sem;

// Initialize the queues
void init_queues() {
    scheduler_queue = malloc(sizeof(ProcessQueue));
    terminated_queue = malloc(sizeof(ProcessQueue));
    scheduler_queue->rear = -1;
    terminated_queue->rear = -1;
    sem_init(&scheduler_queue_sem, 0, 1);
}

void print_process_stats() {
    printf("\nProcess Execution Statistics:\n");
    printf("PID\tName\t\tPriority\tExecution Time(ms)\tWaiting Time(ms)\n");
    printf("------------------------------------------------------------------------\n");
    
    for (int i = 0; i <= terminated_queue->rear; i++) {
        ProcessInfo *proc = &terminated_queue->processes[i];
        printf("%d\t%-15s\t%d\t\t%lld\t\t\t%lld\n",
               proc->pid,
               proc->name,
               proc->priority,
               proc->total_execution_time,
               proc->waiting_time);
    }
    printf("\n");
}

void manage_SIGCHLD(int signo) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        sem_wait(&scheduler_queue_sem);

        // Find and update the finished process
        for (int i = 0; i <= scheduler_queue->rear; i++) {
            if (scheduler_queue->processes[i].pid == pid) {
                // Mark as finished and calculate times
                scheduler_queue->processes[i].state = -1;
                gettimeofday(&scheduler_queue->processes[i].end_time, NULL);
                
                // Calculate execution time
                struct timeval elapsed;
                timersub(&scheduler_queue->processes[i].end_time, 
                        &scheduler_queue->processes[i].start_time, 
                        &elapsed);
                long long exec_time = elapsed.tv_sec * 1000 + elapsed.tv_usec / 1000;
                
                scheduler_queue->processes[i].total_execution_time = exec_time;
                scheduler_queue->processes[i].waiting_time = exec_time;

                // Move to terminated queue
                terminated_queue->rear++;
                terminated_queue->processes[terminated_queue->rear] = 
                    scheduler_queue->processes[i];

                // Remove from scheduler queue
                for (int j = i; j < scheduler_queue->rear; j++) {
                    scheduler_queue->processes[j] = scheduler_queue->processes[j + 1];
                }
                scheduler_queue->rear--;
                break;
            }
        }
        
        sem_post(&scheduler_queue_sem);
    }
}

void manage_SIGINT(int signo) {
    print_process_stats();
    
    // Cleanup and exit
    sem_destroy(&scheduler_queue_sem);
    free(scheduler_queue);
    free(terminated_queue);
    exit(0);
}

void manage_SIGUSR1(int signo) {
    printf("\nStarting execution of submitted programs...\n");
    
    sem_wait(&scheduler_queue_sem);
    
    // Resume all stopped processes
    for (int i = 0; i <= scheduler_queue->rear; i++) {
        if (scheduler_queue->processes[i].state != -1) {
            kill(scheduler_queue->processes[i].pid, SIGCONT);
            gettimeofday(&scheduler_queue->processes[i].start_time, NULL);
        }
    }
    
    sem_post(&scheduler_queue_sem);
}


void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        should_exit = 1;
    } else if (signum == SIGUSR1) {
        start_execution = 1;
        //printf("Received start signal. Beginning execution...\n");
    }
}

// [Previous helper functions remain the same: initialize_queue, enqueue, dequeue]
void initialize_queue() {
    ready_queue.count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        ready_queue.processes[i] = NULL;
    }
}

void enqueue(PCB *process) {
    if (ready_queue.count >= MAX_PROCESSES) return;
    
    int i = ready_queue.count - 1;
    while (i >= 0 && ready_queue.processes[i]->priority < process->priority) {
        ready_queue.processes[i + 1] = ready_queue.processes[i];
        i--;
    }
    ready_queue.processes[i + 1] = process;
    ready_queue.count++;
}

PCB* dequeue() {
    if (ready_queue.count == 0) return NULL;
    PCB *process = ready_queue.processes[0];
    
    for (int i = 0; i < ready_queue.count - 1; i++) {
        ready_queue.processes[i] = ready_queue.processes[i + 1];
    }
    ready_queue.count--;
    return process;
}

void print_final_status() {
    printf("PID\t");
    printf("Name\t\t");
    printf("State\t\t");
    printf("Exec Time\t");
    printf("Wait Time\t");
    printf("Priority\n");
    printf("-------------------------------------------------------------------------\n");
    
    for (int i = 0; i < total_processes; i++) {
        if (process_table[i] != NULL) {
            const char* state_str;
            
            if (process_table[i]->state == STATE_CREATED) 
            {
                state_str = "CREATED"; 
            } 
            else if (process_table[i]->state == STATE_READY) 
            {
                state_str = "READY"; 
            } 
            else if (process_table[i]->state == STATE_RUNNING) 
            {
                state_str = "RUNNING"; 
            } 
            else if (process_table[i]->state == STATE_FINISHED) {
                state_str = "FINISHED"; 
            } 
            else {
                state_str = "UNKNOWN"; 
            }
            
            printf("%d\t", process_table[i]->pid); 
            printf("%-15s\t", process_table[i]->name);  
            printf("%-10s\t", state_str); 
            printf("%ld ms\t\t", process_table[i]->total_exec_time * TSLICE); 
            printf("%ld ms\t\t", process_table[i]->total_wait_time * TSLICE);  
            printf("%d\n", process_table[i]->priority);  
        }
    }
    printf("\n");
}

void handle_finished_process(pid_t pid) {
    int i = 0; // Initialize the index
    while (i < total_processes) {
        if (process_table[i] != NULL && process_table[i]->pid == pid) {
            process_table[i]->state = STATE_FINISHED;
            process_table[i]->is_running = 0;
            break; 
        }
        i++; 
    }
}

void free_resource() {
    // Stop and cleanup all processes
    for (int i = 0; i < total_processes; i++) {
        if (process_table[i] != NULL) {
            if (process_table[i]->is_running) {
                kill(process_table[i]->pid, SIGTERM);
            }
            waitpid(process_table[i]->pid, NULL, 0);
        }
    }

    // Print final statistics only on exit
    print_final_status();

    // Cleanup memory and message queue
    for (int i = 0; i < total_processes; i++) {
        if (process_table[i] != NULL) {
            free(process_table[i]);
        }
    }
    
    if (msgq_id != -1) {
        msgctl(msgq_id, IPC_RMID, NULL);
    }
}
void schedule_processes() {
    if (!start_execution) return;  // do not schedule until start signal received
    
    int status;
    pid_t finished_pid;
    
    while ((finished_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        handle_finished_process(finished_pid);
    }

    // Stop currently running processes and update their stats
    int i = 0;
    while (i < total_processes) {
        if (process_table[i] != NULL && process_table[i]->is_running) {
            kill(process_table[i]->pid, SIGSTOP);
            process_table[i]->is_running = 0;
            if (process_table[i]->state != STATE_FINISHED) {
                process_table[i]->total_exec_time++;
                process_table[i]->state = STATE_READY;
                enqueue(process_table[i]);
            }
        }
        i++; 
    }

    // Start new processes
    int running_processes = 0;
    while (running_processes < NCPU && ready_queue.count > 0) {
        PCB *current = dequeue();
        if (current && current->state != STATE_FINISHED) {
            kill(current->pid, SIGCONT);
            current->is_running = 1;
            current->state = STATE_RUNNING;
            running_processes++;
        }
    }

    // Update wait times
    int j = 0; // Initialize index for while loop
    while (j < total_processes) {
        if (process_table[j] != NULL && process_table[j]->state != STATE_FINISHED && 
        !process_table[j]->is_running) {
            process_table[j]->total_wait_time++;
        }
    i++; 
}

}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <NCPU> <TSLICE>\n", argv[0]);
        exit(1);
    }
    init_queues();

    if (signal(SIGINT, manage_SIGINT) == SIG_ERR){
        printf("error in SIGNING");
    }
    // Setting the function for SIGCHLD
    if (signal(SIGCHLD, manage_SIGCHLD) == SIG_ERR){
        printf("error in SIGCHILD");
    }
    if (signal(SIGUSR1, manage_SIGUSR1)== SIG_ERR){
        printf("error in SIGUSR1");
    }

    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);
    
    initialize_queue();
    
    msgq_id = msgget(MSGQ_KEY, 0666 | IPC_CREAT);
    if (msgq_id == -1) {
        perror("msgget failed");
        exit(1);
    }

    //printf("Scheduler started with %d CPUs and %d ms time slice\n", NCPU, TSLICE);
    printf("scheduler started with ");
    printf("%d CPUs ", NCPU);
    printf("\n");
    printf("%d ms time slice\n", TSLICE);
    printf("\n");
    //printf("Waiting for processes to be submitted...\n");
    //printf("Send SIGUSR1 to start execution, SIGINT to exit\n");

    while (!should_exit) {
        // Check for new process submissions
        if (msgrcv(msgq_id, &message, sizeof(message) - sizeof(long), 1, IPC_NOWAIT) != -1) {
            PCB *new_process = malloc(sizeof(PCB));
            strncpy(new_process->name, message.command, MAXIMUM_COMMANDS);
            new_process->priority = message.priority;
            new_process->state = STATE_CREATED;
            new_process->total_exec_time = 0;
            new_process->total_wait_time = 0;
            new_process->is_running = 0;
            new_process->start_time = time(NULL);
            
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);
                signal(SIGTERM, SIG_DFL);
                raise(SIGSTOP);  // Stop immediately
                execl(message.command, message.command, NULL);
                exit(1);
            } else {
                new_process->pid = pid;
                process_table[total_processes++] = new_process;

                sem_wait(&scheduler_queue_sem);
                scheduler_queue->rear++;
                scheduler_queue->processes[scheduler_queue->rear].pid = pid;
                strncpy(scheduler_queue->processes[scheduler_queue->rear].name, message.command, 255);
                scheduler_queue->processes[scheduler_queue->rear].priority = message.priority;
                scheduler_queue->processes[scheduler_queue->rear].state = 0;
                sem_post(&scheduler_queue_sem);

                printf("Process submitted: %s (PID: %d, Priority: %d)\n", 
                       message.command, pid, message.priority);
                
                int status;
                waitpid(pid, &status, WUNTRACED);
                new_process->state = STATE_READY;
                enqueue(new_process);
            }
        }

        schedule_processes();
        usleep(TSLICE * 1000);
    }

    free_resource();
    return 0;
}
