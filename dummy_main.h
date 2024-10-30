#ifndef DUMMY_MAIN_H
#define DUMMY_MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

// to replace the main of any executable
int dummy_main(int argc, char **argv);

// Signal handler for SIGINT
void handle_sigint(int signum) {
  printf("Process %d received SIGINT: Handling interrupt\n", getpid());
}

void setup_signal_handling() {
    // Register signal handler for SIGINT
    signal(SIGINT, handle_sigint);
    printf("Process %d is waiting for SIGINT\n", getpid());
}

int main(int argc, char **argv) {
    // Setup signal handling
    setup_signal_handling();
    // Call the original logic inside dummy_main
    return dummy_main(argc,argv);
}

#define main dummy_main

#endif 
