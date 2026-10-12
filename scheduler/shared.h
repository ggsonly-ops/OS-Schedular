#ifndef SHARED_H
#define SHARED_H

#include <sys/types.h>

#define MAX_PROCESSES 100
#define MAX_NAME_LEN 256
#define SHM_KEY 12345
#define SEM_KEY 54321

/* Process information structure */
typedef struct {
    pid_t pid;
    char name[MAX_NAME_LEN];
    int job_entry_tslice;
    int wait_time;
    int completion_time;
    int is_active;
    int queue_entry_tslice;
} ProcessInfo;

/* Shared memory structure */
typedef struct {
    int tslice;
    int ncpu;
    int tslice_duration;
    
    /* Ready queue */
    pid_t ready_queue[MAX_PROCESSES];
    int ready_front;
    int ready_rear;
    int ready_count;
    
    /* Running queue */
    pid_t running_queue[MAX_PROCESSES];
    int running_count;
    
    /* Process info array */
    ProcessInfo processes[MAX_PROCESSES];
    int process_count;
    
    /* Scheduler control */
    int scheduler_active;
    int shell_active;
} SharedMemory;

#endif