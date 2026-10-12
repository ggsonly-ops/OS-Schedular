# Round-Robin Process Scheduler and Unix Shell in C

Two Linux systems programs written from scratch in C with raw POSIX calls:

1. **`scheduler/`**: a multi-CPU round-robin scheduler daemon plus a shell that submits jobs to it.
2. **`pipe-shell/`**: an interactive Unix shell with multi-stage pipes, command history and timing.

Built for the Operating Systems course (CSE231) at IIIT-Delhi.

## 1. Round-robin scheduler

The shell and the scheduler are separate processes. They share a process table through **System V shared memory** and guard it with a **System V semaphore**.

```
 simpleshell ──submit ./job──▶ fork + exec, job blocks on SIGCONT
      │                               │
      │   shared memory (ready queue, running queue, process table)
      │   + semaphore                 │
      ▼                               ▼
 simplescheduler: every TSLICE ms
   1. drop finished PIDs from the ready and running queues
   2. SIGSTOP the running jobs and move them to the back of the ready queue
   3. SIGCONT up to NCPU jobs from the front
```

- **`simplescheduler.c`**: the daemon. It preempts with `SIGSTOP` and resumes with `SIGCONT`, and prints a timeline for every slice (`CPU1: <pid> | CPU2: Idle | Ready=[...]`).
- **`simpleshell.c`**: creates the shared memory segment and the semaphore, forks and execs the scheduler, and accepts `submit <executable>`. When you exit, it waits for every job and prints each one's completion time and wait time.
- **`dummy_main.h`**: wraps a job's `main()` so the job waits for `SIGCONT` before it runs. A job therefore starts only when the scheduler admits it.
- **`shared.h`**: the shared-memory layout (queues, a 100-entry process table, `ncpu`, time slice).

```bash
make
cd scheduler
./simpleshell 2 200          # 2 CPUs, 200 ms time slice
SimpleShell> submit ./sample_job
SimpleShell> submit ./sample_job
SimpleShell> submit ./sample_job
SimpleShell> exit
```

With 2 CPUs and 3 CPU-bound jobs, the timeline shows the third job waiting in `Ready=[...]` and rotating onto a CPU each slice. The statistics table at exit reports per-job completion and wait time.

## 2. Shell with pipes

`pipe-shell/code.c`:

- **Pipelines of any length**: splits on `|`, creates one `pipe()` per link, forks one child per stage and wires them with `dup2()`. Every unused pipe end is closed in every process, so no stage blocks forever waiting for EOF.
- **History**: every command is stored with its PID and start and end times (`gettimeofday`). `history` lists them.
- **Ctrl+C**: a `SIGINT` handler prints the full history with each command's PID and run time, then exits.

```bash
cd pipe-shell && ./myshell
UNIX_Shell> echo hello world | tr a-z A-Z | rev
DLROW OLLEH
UNIX_Shell> cat file.txt | grep lion | wc -l
```

## Concepts

`fork` / `execvp` / `waitpid`, signals (`SIGSTOP`, `SIGCONT`, `SIGINT`, `sigaction`), System V IPC (`shmget`, `shmat`, `semget`, `semop`), `pipe` and `dup2`, file-descriptor hygiene, and round-robin time slicing.
