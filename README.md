# OS-Scheduler
🧠 Simple Linux Job Scheduler & Shell

A lightweight Unix-like shell combined with a custom job scheduler, implemented in C, demonstrating core Operating Systems concepts such as process creation, scheduling, inter-process communication, and synchronization.

This project simulates how an OS manages user-submitted jobs using a scheduler while providing a shell interface for interaction.

📌 Project Overview

This project consists of two main components:

Simple Shell

Accepts user commands

Allows submission of jobs

Interacts with the scheduler

Simple Scheduler

Manages and schedules jobs

Executes jobs using Linux process primitives

Demonstrates CPU scheduling behavior

The system mimics basic OS scheduling mechanisms using real Linux system calls.

🧩 Key Features

🖥️ Custom interactive shell

⚙️ Job submission and execution

⏱️ Scheduling logic implemented in C

🧵 Process creation using fork() and exec()

🔁 Job queue management

🔗 Shared data structures for coordination

🧪 Sample job programs for testing

🗂️ Project Structure
.
├── simpleshell.c        # Interactive shell for user commands
├── simplescheduler.c    # Core scheduler logic
├── shared.h             # Shared structures & constants
├── dummy_main.h         # Helper header for job execution
├── sample_job.c         # Sample job program
├── scheduler.pdf        # Design / explanation document
├── Contribution.pdf     # Team contribution details
└── README.md            # Project documentation

🛠️ Technologies Used

Language: C

Platform: Linux / Unix-based systems

Concepts:

Process Management

CPU Scheduling

Fork–Exec Model

IPC (Shared headers / coordination)

Operating Systems Design

⚙️ How It Works (High-Level)

The shell reads user commands.

Jobs are submitted through the shell.

The scheduler maintains a queue of jobs.

Jobs are executed using Linux processes.

Scheduler decides when and which job runs.

Sample jobs simulate CPU-bound work.

▶️ Compilation & Execution
Compile
gcc simpleshell.c -o shell
gcc simplescheduler.c -o scheduler
gcc sample_job.c -o sample_job

Run Scheduler
./scheduler

Run Shell (in another terminal)
./shell

🧪 Sample Job

The sample_job.c file simulates a basic job that can be scheduled and executed by the scheduler.
You can modify or extend it to test different workloads.

📚 Concepts Demonstrated

Process lifecycle

Context switching (conceptual)

Scheduling policies (simplified)

System calls: fork, exec, wait

Modular OS-style design

👥 Team Contributions

Detailed individual contributions are documented in:

Contribution.pdf
