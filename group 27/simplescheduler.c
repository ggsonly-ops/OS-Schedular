

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include "shared.h"

SharedMemory *shm = NULL;
int shm_id = -1;
int sem_id = -1;
volatile sig_atomic_t terminate = 0;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void sem_wait_scheduler(int sem_id) {
    struct sembuf sb = {0, -1, 0};
    if (semop(sem_id, &sb, 1) < 0) {
        perror("semop wait failed");
    }
}

void sem_signal_scheduler(int sem_id) {
    struct sembuf sb = {0, 1, 0};
    if (semop(sem_id, &sb, 1) < 0) {
        perror("semop signal failed");
    }
}

void sigterm_handler(int sig) {
    (void)sig;
    terminate = 1;
}

/* Check whether a PID corresponds to a live process */
int is_process_alive(pid_t pid) {
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return 0;
}

/* Rebuild ready queue excluding dead processes */
void remove_dead_from_ready() {
    int new_count = 0;
    pid_t temp[MAX_PROCESSES];

    int idx = shm->ready_front;
    for (int i = 0; i < shm->ready_count; i++) {
        pid_t pid = shm->ready_queue[idx];
        if (is_process_alive(pid)) {
            temp[new_count++] = pid;
        } else {
            for (int j = 0; j < shm->process_count; j++) {
                if (shm->processes[j].pid == pid) {
                    shm->processes[j].is_active = 0;
                    break;
                }
            }
        }
        idx = (idx + 1) % MAX_PROCESSES;
    }

    shm->ready_front = 0;
    shm->ready_rear = new_count % MAX_PROCESSES;
    shm->ready_count = new_count;
    for (int i = 0; i < new_count; i++) {
        shm->ready_queue[i] = temp[i];
    }
}

/* Remove dead processes from running_queue */
void remove_dead_from_running() {
    int new_count = 0;
    for (int i = 0; i < shm->running_count; i++) {
        pid_t pid = shm->running_queue[i];
        if (is_process_alive(pid)) {
            shm->running_queue[new_count++] = pid;
        } else {
            for (int j = 0; j < shm->process_count; j++) {
                if (shm->processes[j].pid == pid) {
                    shm->processes[j].is_active = 0;
                    break;
                }
            }
        }
    }
    shm->running_count = new_count;
}

/* Return a human-friendly ReadyQueue string for printing */
void build_ready_queue_str(char *buf, size_t buflen) {
    size_t pos = 0;
    pos += snprintf(buf + pos, buflen - pos, "[");
    int idx = shm->ready_front;
    for (int i = 0; i < shm->ready_count; i++) {
        pos += snprintf(buf + pos, buflen - pos, "%d", shm->ready_queue[idx]);
        if (i != shm->ready_count - 1) pos += snprintf(buf + pos, buflen - pos, ", ");
        idx = (idx + 1) % MAX_PROCESSES;
    }
    pos += snprintf(buf + pos, buflen - pos, "]");
}

/* Print a single slice log row */
void print_slice_log(int slice_num, int tslice_ms) {
    long start_time = (long)(slice_num - 1) * tslice_ms;
    long end_time = (long)(slice_num) * tslice_ms;

    char cpus[4096] = {0};
    size_t pos = 0;
    for (int c = 0; c < shm->ncpu; c++) {
        if (c < shm->running_count) {
            pos += snprintf(cpus + pos, sizeof(cpus) - pos, "CPU%d: %d", c + 1, shm->running_queue[c]);
        } else {
            pos += snprintf(cpus + pos, sizeof(cpus) - pos, "CPU%d: Idle", c + 1);
        }
        if (c != shm->ncpu - 1) pos += snprintf(cpus + pos, sizeof(cpus) - pos, " | ");
    }

    char readybuf[1024];
    build_ready_queue_str(readybuf, sizeof(readybuf));

    printf("Slice %2d | %5ld - %5ld ms | %s | Ready=%s\n",
           slice_num, start_time, end_time, cpus, readybuf);
    fflush(stdout);
}

/* Core scheduling function called at each global tick (slice boundary) */
void schedule_tick() {
    sem_wait_scheduler(sem_id);

    /* Increment global slice counter */
    shm->tslice++;

    int current_slice = shm->tslice;
    int tslice_ms = shm->tslice_duration;

    /* Clean up any dead PIDs from running and ready queues */
    remove_dead_from_running();
    remove_dead_from_ready();

    /* Print the slice summary showing who ran in the previous interval */
    print_slice_log(current_slice, tslice_ms);

    /* Stop currently running processes and move them to rear of ready queue only if they are alive & active */
    for (int i = 0; i < shm->running_count; i++) {
        pid_t pid = shm->running_queue[i];
        if (!is_process_alive(pid)) {
            continue;
        }

        /* Send SIGSTOP to preempt */
        if (kill(pid, SIGSTOP) < 0) {
            /* Could fail if process just exited; ignore */
        } else {
            /* Requeue alive processes */
            shm->ready_queue[shm->ready_rear] = pid;
            shm->ready_rear = (shm->ready_rear + 1) % MAX_PROCESSES;
            shm->ready_count++;

            /* CRITICAL: Set queue_entry_tslice to current slice
               When this process is scheduled in THIS SAME TICK, waited_slices will be 0 */
            for (int j = 0; j < shm->process_count; j++) {
                if (shm->processes[j].pid == pid) {
                    shm->processes[j].queue_entry_tslice = shm->tslice;
                    break;
                }
            }
        }
    }

    /* Clear running queue */
    shm->running_count = 0;

    /* Remove any dead entries from ready queue */
    remove_dead_from_ready();

    /* Select up to NCPU processes from ready queue and run them */
    int to_schedule = shm->ncpu < shm->ready_count ? shm->ncpu : shm->ready_count;
    for (int i = 0; i < to_schedule; i++) {
        pid_t pid = shm->ready_queue[shm->ready_front];
        shm->ready_front = (shm->ready_front + 1) % MAX_PROCESSES;
        shm->ready_count--;

        if (!is_process_alive(pid)) {
            continue;
        }

        /* Calculate wait time */
        for (int j = 0; j < shm->process_count; j++) {
            if (shm->processes[j].pid == pid) {
                /* Wait time = how many slices this process spent in ready queue
                   If queue_entry_tslice < current_slice, it was waiting */
                int waited_slices = shm->tslice - shm->processes[j].queue_entry_tslice;
                if (waited_slices > 0) {
                    shm->processes[j].wait_time += waited_slices * shm->tslice_duration;
                }
                break;
            }
        }

        /* Add to running queue */
        shm->running_queue[shm->running_count++] = pid;

        /* Send SIGCONT to resume/start */
        if (kill(pid, SIGCONT) < 0) {
            /* could fail if process ended */
        }
    }

    sem_signal_scheduler(sem_id);
}

/* Attach to shared memory and run scheduler loop */
int main() {
    /* Attach to shared memory */
    shm_id = shmget(SHM_KEY, sizeof(SharedMemory), 0666);
    if (shm_id < 0) {
        perror("simplescheduler: shmget failed");
        return 1;
    }

    shm = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) {
        perror("simplescheduler: shmat failed");
        return 1;
    }

    /* Get semaphore */
    sem_id = semget(SEM_KEY, 1, 0666);
    if (sem_id < 0) {
        perror("simplescheduler: semget failed");
        shmdt(shm);
        return 1;
    }

    /* Signal handling */
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT, sigterm_handler);

    int tslice_ms = shm->tslice_duration;

    /* Print header for timeline table */
    printf("=== Round-Robin scheduler timeline (TSLICE = %d ms, NCPU = %d) ===\n",
           tslice_ms, shm->ncpu);
    printf("Slice | Interval (ms) | CPUs running ... | Ready Queue\n");
    printf("---------------------------------------------------------------\n");
    fflush(stdout);

    /* Main loop: tick every tslice_ms, then perform scheduling actions */
    while (!terminate) {
        usleep((useconds_t)tslice_ms * 1000);

        /* If shell has terminated and no active jobs and both queues empty, exit */
        sem_wait_scheduler(sem_id);
        int has_active = 0;
        for (int i = 0; i < shm->process_count; i++) {
            if (shm->processes[i].is_active && is_process_alive(shm->processes[i].pid)) {
                has_active = 1;
                break;
            }
        }
        int shell_active = shm->shell_active;
        sem_signal_scheduler(sem_id);

        /* Do scheduling actions for this tick */
        schedule_tick();

        /* If shell is not active and no jobs, terminate */
        sem_wait_scheduler(sem_id);
        int ready = shm->ready_count;
        int running = shm->running_count;
        sem_signal_scheduler(sem_id);

        if (!shell_active && !has_active && ready == 0 && running == 0) {
            break;
        }
    }

    /* On termination: stop all running processes */
    sem_wait_scheduler(sem_id);
    for (int i = 0; i < shm->running_count; i++) {
        pid_t pid = shm->running_queue[i];
        if (is_process_alive(pid)) {
            kill(pid, SIGSTOP);
        }
    }
    sem_signal_scheduler(sem_id);

    /* Detach */
    shmdt(shm);
    return 0;
}