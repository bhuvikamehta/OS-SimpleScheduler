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
#include <libgen.h> 
#include <semaphore.h>
#include <sys/time.h>

#define MAXIMUM_PROCESSES 100
#define MAXIMUM_COMMANDS 512

#define INPUT_BUFFER_SIZE 1024
#define MESSAGE_QUEUEKEY 100

// process states 
#define STATE_CREATED 0
#define STATE_READY 1
#define STATE_RUNNING 2
#define STATE_FINISHED 3

// message structure for IPC
struct message_buffer {
    long message_type;
    char command[MAXIMUM_COMMANDS];
    int priority;
} message;

// PCB structure to store proccess details 
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
    PCB *processes[MAXIMUM_PROCESSES];
    int count;
} ReadyQueue;

ReadyQueue ready_queue;
int message_queueid;
int NCPU;
int TSLICE;
int total_processes = 0;
PCB *process_table[MAXIMUM_PROCESSES];
volatile sig_atomic_t should_exit = 0;
volatile sig_atomic_t start_execution = 0;



typedef struct {
    //tracking process state
    int state;
    struct timeval start_time;  
    struct timeval end_time;
    long long total_execution_time;
    long long waiting_time;
    pid_t pid;
    char name[256];
    int priority;
} taskinfo;

typedef struct {
    taskinfo processes[MAXIMUM_PROCESSES];
    int rear;
} process_queue;

process_queue *scheduler_queue;
process_queue *terminated_queue;
sem_t scheduler_queue_sem;

// Initialize the queues
void init_queues() {
    scheduler_queue = malloc(sizeof(process_queue));
    scheduler_queue->rear = -1;
    terminated_queue = malloc(sizeof(process_queue));
    terminated_queue->rear = -1;
    sem_init(&scheduler_queue_sem, 0, 1);
}

void print_process_stats() {
    
    printf("\n");
    printf("P_id\tname\t\tpriority\texecution time\twaiting time");
    printf("\n");
    printf("------------------------------------------------------------------------");
    printf("\n");
    
    for (int i = 0; i <= terminated_queue->rear; i++) {
        taskinfo *proc = &terminated_queue->processes[i];
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
                scheduler_queue->processes[i].state = -1;
                gettimeofday(&scheduler_queue->processes[i].end_time, NULL); // calculate times
                
                struct timeval elapsed; //execution time
                timersub(&scheduler_queue->processes[i].end_time, 
                        &scheduler_queue->processes[i].start_time, 
                        &elapsed);
                long long exec_time = elapsed.tv_sec * 1000 + elapsed.tv_usec / 1000;
                
                scheduler_queue->processes[i].total_execution_time = exec_time;
                scheduler_queue->processes[i].waiting_time = exec_time;

                
                terminated_queue->rear++;
                terminated_queue->processes[terminated_queue->rear] = scheduler_queue->processes[i]; // move to terminated queue

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

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        should_exit = 1;
    } else if (signum == SIGUSR1) {
        start_execution = 1;
    }
}
void manage_SIGUSR1(int signo) {
    
    sem_wait(&scheduler_queue_sem);
    
    for (int i = 0; i <= scheduler_queue->rear; i++) {
        if (scheduler_queue->processes[i].state != -1) {             // resume all stopped processes
            kill(scheduler_queue->processes[i].pid, SIGCONT);
            gettimeofday(&scheduler_queue->processes[i].start_time, NULL); 
        }
    }
    
    sem_post(&scheduler_queue_sem);
}

void enqueue(PCB *process) {
    if (ready_queue.count >= MAXIMUM_PROCESSES) return;
    
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
    printf("pid\t");
    printf("name\t\t");
    printf("state\t\t");
    printf("exec time\t");
    printf("wait time\t");
    printf("priority\n");
    printf("-------------------------------------------------------------------------");
    printf("\n");
    
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
    int i = 0; 
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

    for (int i = 0; i < total_processes; i++) {     //cleanup all the processes
        if (process_table[i] != NULL) {
            if (process_table[i]->is_running) {
                kill(process_table[i]->pid, SIGTERM);
            }
            waitpid(process_table[i]->pid, NULL, 0);
        }
    }

    print_final_status(); //on exit

    for (int i = 0; i < total_processes; i++) {
        if (process_table[i] != NULL) {
            free(process_table[i]);
        }       //cleaning up memory and message queue
    }
    
    if (message_queueid != -1) {
        msgctl(message_queueid, IPC_RMID, NULL);
    }
}
void RoundRobin() {
    if (!start_execution) return; 
    
    int status;
    pid_t finished_pid;
    
    while ((finished_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        handle_finished_process(finished_pid);
    }

    int i = 0;
    while (i < total_processes) {       //stop running processes
        if (process_table[i] != NULL && process_table[i]->is_running) {             //update their state
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

    int running_processes = 0;                  //new process
    while (running_processes < NCPU && ready_queue.count > 0) {
        PCB *current = dequeue();
        if (current && current->state != STATE_FINISHED) {
            kill(current->pid, SIGCONT);
            current->is_running = 1;
            current->state = STATE_RUNNING;
            running_processes++;
        }
    }

    
    int j = 0;                                      
    while (j < total_processes) {
        if (process_table[j] != NULL) {
            if (process_table[j]->state != STATE_FINISHED && !process_table[j]->is_running){
                process_table[j]->total_wait_time++;                    //wait time
            }
        }
    j++; 
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
    
    ready_queue.count = 0;
    for (int i = 0; i < MAXIMUM_PROCESSES; i++) {
        ready_queue.processes[i] = NULL;
    }
    
    message_queueid = msgget(MESSAGE_QUEUEKEY, 0666 | IPC_CREAT);
    if (message_queueid == -1) {
        printf("message queue cannot be created");
        printf("\n");
        exit(1);
    }

    printf("scheduler started with ");
    printf("%d CPUs ", NCPU);
    printf("\n");
    printf("%d ms time slice\n", TSLICE);
   
    while (!should_exit) {
       
        if (msgrcv(message_queueid, &message, sizeof(message) - sizeof(long), 1, IPC_NOWAIT) != -1) {  //new process submissions
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
                raise(SIGSTOP); 
                execl(message.command, message.command, NULL);
                exit(1);
            } 
            else {
                new_process->pid = pid;
                process_table[total_processes++] = new_process;

                sem_wait(&scheduler_queue_sem);
                scheduler_queue->rear++;
                scheduler_queue->processes[scheduler_queue->rear].pid = pid;
                strncpy(scheduler_queue->processes[scheduler_queue->rear].name, message.command, 255);
                scheduler_queue->processes[scheduler_queue->rear].priority = message.priority;
                scheduler_queue->processes[scheduler_queue->rear].state = 0;
                sem_post(&scheduler_queue_sem);

                printf("(pid: %d, ", pid);
                printf("priority: %d)\n", message.priority);
                
                int status;
                waitpid(pid, &status, WUNTRACED);
                new_process->state = STATE_READY;
                enqueue(new_process);
            }
        }

        RoundRobin();
        usleep(TSLICE * 1000);
    }

    free_resource();
    return 0;
}
