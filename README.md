# OS-Assignment-3-SimpleScheduler

OS Assignment-3: Simple Scheduler: A Process Scheduler in C from Scratch


We have attempted with bonus.


Group ID- 58
Group Members:
Bhuvika Mehta (2023172)
Pragya Singh (2023379)


Github Repository link- https://github.com/bhuvikamehta/OS-Assignment-3-SimpleScheduler


Contribution:
Both contributed equally and were present at all times. The task was distributed equally within both members.


Implementation:
The Simple Scheduler program is designed to manage and schedule multiple processes in a multi-core environment, using round-robin and optional priority-based scheduling. It includes features for process states, inter-process communication (IPC), signal handling, and statistics tracking. 
The main loop iterates through each process, assigning it to a core based on round-robin scheduling. Within each cycle, processes execute for a time slice, with state changes handled by signal-based callbacks.
It includes round robin scheduling across multiple cores defined by NCPU. In addition to round robin, priority based scheduling can also be enabled. Processes with higher priority are executed before those with lower priority.
Processes can be in one of several states: RUNNING, WAITING, or TERMINATED. State transitions are managed using signals, allowing the scheduler to respond to changes in real-time. A process will execute only after you give the program a signal, "execute" command in this case.
The program also handles key signals like SIGCHLD, SIGINT, SIGUSR1.
It used IPC mechanisms like IPC semaphores and message queues for process coordination.
On program termination, the statistics such as execution time, wait time, and termination status are displayed, providing a summary of the scheduler's performance.
