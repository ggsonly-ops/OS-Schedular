
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <errno.h>
#include "shared.h"

SharedMemory *shm = NULL;
int shm_id = -1;
int sem_id = -1;
pid_t scheduler_pid = -1;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

void sem_wait_s(int sem_id) {
    struct sembuf sb = {0, -1, 0};
    if (semop(sem_id, &sb, 1) < 0) {
        perror("semop wait failed (shell)");
    }
}

void sem_signal_s(int sem_id) {
    struct sembuf sb = {0, 1, 0};
    if (semop(sem_id, &sb, 1) < 0) {
        perror("semop signal failed (shell)");
    }
}

void cleanup() {
    if (shm != NULL) {
        shm->shell_active = 0;
        shmdt(shm);
        shm = NULL;
    }

    if (scheduler_pid > 0) {
        kill(scheduler_pid, SIGTERM);
        waitpid(scheduler_pid, NULL, 0);
    }

    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        sem_id = -1;
    }

    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        shm_id = -1;
    }
}

void add_to_ready_queue(pid_t pid) {
    sem_wait_s(sem_id);

    shm->ready_queue[shm->ready_rear] = pid;
    shm->ready_rear = (shm->ready_rear + 1) % MAX_PROCESSES;
    shm->ready_count++;

    /* Set queue_entry_tslice to NEXT slice (current + 1)
       Because scheduler will run at the END of current slice, and this job
       will be picked up in the next scheduling round */
    for (int i = 0; i < shm->process_count; i++) {
        if (shm->processes[i].pid == pid) {
            shm->processes[i].queue_entry_tslice = shm->tslice + 1;
            break;
        }
    }

    sem_signal_s(sem_id);
}

void add_process_info(pid_t pid, const char *name) {
    sem_wait_s(sem_id);

    if (shm->process_count < MAX_PROCESSES) {
        ProcessInfo *p = &shm->processes[shm->process_count];
        p->pid = pid;
        strncpy(p->name, name, MAX_NAME_LEN - 1);
        p->name[MAX_NAME_LEN - 1] = '\0';
        p->job_entry_tslice = shm->tslice;
        p->wait_time = 0;
        p->completion_time = 0;
        p->is_active = 1;
        p->queue_entry_tslice = 0;  // Will be set properly in add_to_ready_queue
        shm->process_count++;
    } else {
        fprintf(stderr, "Warning: process table full, cannot record process info\n");
    }

    sem_signal_s(sem_id);
}

void handle_submit(char *command) {
    char *token = strtok(command, " \t\n");
    if (token == NULL || strcmp(token, "submit") != 0) {
        printf("Invalid command\n");
        return;
    }

    token = strtok(NULL, " \t\n");
    if (token == NULL) {
        printf("Usage: submit <executable>\n");
        return;
    }

    char executable[MAX_NAME_LEN];
    strncpy(executable, token, MAX_NAME_LEN - 1);
    executable[MAX_NAME_LEN - 1] = '\0';

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        /* Child: exec user program */
        execl(executable, executable, NULL);
        perror("execl failed");
        _exit(1);
    } else {
        /* Parent: record process info and add to ready queue */
        printf("Job submitted with PID: %d\n", pid);
        add_process_info(pid, executable);
        add_to_ready_queue(pid);
    }
}

/* Print final statistics */
void print_statistics() {
    sem_wait_s(sem_id);

    printf("\n========== Job Statistics ==========\n");
    printf("%-8s %-8s %-18s %-18s\n", "PID", "Name", "Completion(ms)", "Wait(ms)");
    printf("--------------------------------------------------------------\n");

    for (int i = 0; i < shm->process_count; i++) {
        ProcessInfo *p = &shm->processes[i];
        printf("%-8d %-8s %-18d %-18d\n",
               p->pid, p->name, p->completion_time, p->wait_time);
    }

    sem_signal_s(sem_id);
}

/* SIGCHLD handler */
void sigchld_handler(int sig) {
    (void)sig;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        sem_wait_s(sem_id);

        for (int i = 0; i < shm->process_count; i++) {
            if (shm->processes[i].pid == pid) {
                shm->processes[i].is_active = 0;

                int tslices_elapsed = shm->tslice - shm->processes[i].job_entry_tslice;
                if (tslices_elapsed <= 0) tslices_elapsed = 1;

                shm->processes[i].completion_time = tslices_elapsed * shm->tslice_duration;

                break;
            }
        }

        sem_signal_s(sem_id);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <NCPU> <TSLICE_ms>\n", argv[0]);
        return 1;
    }

    int ncpu = atoi(argv[1]);
    int tslice_ms = atoi(argv[2]);

    if (ncpu <= 0 || tslice_ms <= 0) {
        fprintf(stderr, "NCPU and TSLICE must be positive integers\n");
        return 1;
    }

    /* Create shared memory */
    shm_id = shmget(SHM_KEY, sizeof(SharedMemory), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget failed");
        return 1;
    }

    shm = (SharedMemory *)shmat(shm_id, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat failed");
        return 1;
    }

    /* Initialize shared memory */
    memset(shm, 0, sizeof(SharedMemory));
    shm->tslice = 0;
    shm->ncpu = ncpu;
    shm->tslice_duration = tslice_ms;
    shm->ready_front = 0;
    shm->ready_rear = 0;
    shm->ready_count = 0;
    shm->running_count = 0;
    shm->process_count = 0;
    shm->scheduler_active = 1;
    shm->shell_active = 1;

    /* Create semaphore */
    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (sem_id < 0) {
        perror("semget failed");
        cleanup();
        return 1;
    }

    union semun arg;
    arg.val = 1;
    if (semctl(sem_id, 0, SETVAL, arg) < 0) {
        perror("semctl failed");
        cleanup();
        return 1;
    }

    /* Set up SIGCHLD handler */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        perror("sigaction SIGCHLD failed");
        cleanup();
        return 1;
    }

    /* Fork and exec scheduler */
    scheduler_pid = fork();
    if (scheduler_pid < 0) {
        perror("fork failed");
        cleanup();
        return 1;
    }

    if (scheduler_pid == 0) {
        /* Child: exec simplescheduler */
        execl("./simplescheduler", "simplescheduler", NULL);
        perror("execl simplescheduler failed");
        _exit(1);
    }

    printf("SimpleShell started with NCPU=%d, TSLICE=%dms\n", ncpu, tslice_ms);
    printf("Scheduler PID: %d\n", scheduler_pid);

    /* Shell prompt loop */
    char command[1024];
    while (1) {
        printf("SimpleShell$ ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }

        command[strcspn(command, "\n")] = 0;

        if (strlen(command) == 0) continue;

        if (strcmp(command, "exit") == 0) {
            printf("Exiting SimpleShell...\n");
            break;
        } else if (strncmp(command, "submit", 6) == 0) {
            handle_submit(command);
        } else {
            printf("Unknown command. Available commands: submit, exit\n");
        }
    }

    /* Set shell_active = 0 and wait for remaining jobs */
    sem_wait_s(sem_id);
    shm->shell_active = 0;
    sem_signal_s(sem_id);

    while (1) {
        sem_wait_s(sem_id);
        int active_jobs = 0;
        for (int i = 0; i < shm->process_count; i++) {
            if (shm->processes[i].is_active) active_jobs++;
        }
        sem_signal_s(sem_id);

        if (active_jobs == 0) break;
        sleep(1);
    }

    print_statistics();
    cleanup();

    return 0;
}