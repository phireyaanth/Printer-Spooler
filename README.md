# Printer-Spooler
📠 presi - Printer Spooler (HW4 - CSE 320 Spring 2025)

Author: Phireyaanth Poobalaraj
Course: CSE 320 - Systems Programming
Professor: Eugene Stark
Due Date: April 18, 2025

==============================
🧠 Overview
==============================
This project implements `presi`, a Unix-like printer spooler system in C. It mimics the behavior of a real-world print server using POSIX system calls, signal handling, and I/O redirection. The system supports job queuing, printer status tracking, file type conversion pipelines, and interactive/batch command execution.

==============================
🛠 Features
==============================
- Interactive command-line interface (presi>)
- Batch mode via -i option and output redirection via -o
- File type and printer registration
- Printer eligibility control via bitmaps
- File conversion pipelines with fork, execvp, pipe, dup2
- Signal-safe job control (pause/resume/cancel)
- SIGCHLD handler with race condition protection
- Job lifecycle management
- Event instrumentation using sf_* functions

==============================
📁 Project Structure
==============================
hw4/
├── bin/                # Compiled binaries
├── demo/               # Demo version of presi
├── include/            # Header files (DO NOT MODIFY)
├── lib/                # presi.a and sf_event.h
├── rsrc/               # Command script (presi.cmd)
├── src/                # Source files: main.c, cli.c
├── tests/              # Criterion test files
├── util/               # Printer simulation utilities
├── spool/              # Printer logs and files (auto-generated)
├── Makefile
└── README.md

==============================
🚀 Build & Run
==============================
Build:
    make

Run Interactively:
    ./bin/presi

Run in Batch Mode:
    ./bin/presi -i commands.txt

Redirect Output:
    ./bin/presi -i commands.txt -o output.txt

==============================
🧪 Testing
==============================
    make tests
    # or
    bin/presi_tests -j1 --verbose

==============================
🧹 Cleanup
==============================
    make clean             # Remove binaries
    make clean_spool       # Remove printer files
    make stop_printers     # Stop all running printers
    make show_printers     # List active printers

==============================
🧾 Supported Commands
==============================
help                            Show help menu
quit                            Exit program
type <ext>                      Register a new file type
printer <name> <type>           Register a new printer
conversion <from> <to> <cmd>    Register a file type conversion
print <file> [printer...]       Queue a file for printing
cancel <job_id>                 Cancel an existing job
pause <job_id>                  Pause a running job
resume <job_id>                 Resume a paused job
disable <printer>               Disable a printer
enable <printer>                Enable a printer
printers                        Show printer status
jobs                            Show queued jobs

==============================
🧠 Learning Objectives
==============================
- Master low-level Unix programming
- Gain signal-safe, async-safe coding experience
- Understand inter-process communication and job control
- Build real-time CLI apps using event-driven logic

==============================
🧰 Tools Used
==============================
- GCC (-Wall -Werror -g)
- Valgrind:
    valgrind --leak-check=full --track-fds=yes ./bin/presi
- Criterion testing framework

==============================
🏁 Submission
==============================
    git add .
    git commit -m "Completed HW4 printer spooler"
    git submit hw4

==============================
👨‍💻 Final Thoughts
==============================
This was one of the most challenging and rewarding system-level assignments. It required precise signal handling, robust job control, and careful pipeline construction using low-level C and POSIX APIs. Extensive testing was essential for stability and correctness.
