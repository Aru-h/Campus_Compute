# CampusCompute — Intelligent Job Scheduling and Resource Management

OS Level 3 mini-project (UE25CS242A). A command-line prototype in **C** that demonstrates
CPU scheduling, a threaded logger, shared-memory IPC, Banker's algorithm, worker-failure
detection and resource limits. Every result is produced by running code (nothing is hard-coded).

## Prerequisites

- Ubuntu or WSL Ubuntu
- `gcc`, `make` (install with `sudo apt install build-essential`)
- Nothing else. It uses only POSIX/Linux system calls (`pthread`, `shm_open`, `sem_open`, `fork`, `setrlimit`).

## Build

```
make            # builds ./campuscompute
make clean      # removes build files and campuscompute.log
```

If you do not want to use `make`, the exact build command is:

```
gcc -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE -o campuscompute main.c util.c jobs.c sched.c \
    logger.c ipc.c banker.c worker.c limits.c verify.c demo.c -pthread -lrt
```

## Run

```
./campuscompute help        # list of commands
./campuscompute demo        # run every feature once and print a PASS/FAIL summary
make test                   # 30 automatic checks
```

| Command | Feature | What it shows |
|---|---|---|
| `sched fcfs` / `sched rr -q 3` / `sched priority` | FR1, FR2 | Gantt chart, order, completion / turnaround / waiting / response time, averages |
| `sched compare` | FR2 | All three policies on the same job set, side by side |
| `sched ... -f data/jobs.txt` | FR1 | Jobs read from a file (validated) |
| `sched ... -v` | FR1 | Life cycle of every job (READY → RUNNING → DONE) |
| `logger [-p P] [-c C] [-n N] [-s Q] [-o file]` | FR3 | Producer/consumer threads, bounded queue, log file |
| `ipc` | FR4 | Two processes share data through shared memory + named semaphores, then clean up |
| `banker safe` / `banker unsafe` / `banker -f file` | FR5 | Need matrix, safety algorithm steps, safe sequence or UNSAFE |
| `worker normal` / `worker fail` / `worker crash` / `worker all` | FR6 | `fork()` + `waitpid()`, normal and simulated failures |
| `rlimit nofile` / `rlimit mem` / `rlimit cpu` / `rlimit all` | FR7 | `setrlimit()` and the effect it has |
| `verify` | FR7 | Checks: no child processes, one thread, no leftover IPC objects |
| `cleanup [--logs]` | FR7 | Removes leftover `cc_shm_*` / `cc_sem_*` objects (and the log with `--logs`) |

Invalid commands or input print an error plus a usage message and return exit code 2 (bad usage)
or 1 (bad input data).

## Scheduling rules (FR2)

- **Tie-break:** earlier arrival first; if still tied, smaller job id first.
- **Priority:** non-preemptive; smaller number = higher priority; ties use the rule above.
- **Round Robin:** default quantum **3** (change with `-q`). If a job arrives exactly when a quantum ends,
  it is queued *before* the preempted job.
- `turnaround = completion − arrival`, `waiting = turnaround − burst`, `response = first start − arrival`.

## Job file format

One job per line: `id arrival burst priority`. `#` starts a comment. Limits: up to 20 jobs, burst 1–100, priority 1–99.
See `data/jobs.txt`. Banker's input files are in `data/banker_safe.txt` and `data/banker_unsafe.txt`.

## Cleanup

The program removes everything it creates:
- threads are joined, child processes are collected with `waitpid()`,
- shared memory and semaphores are `munmap`ed, closed and unlinked (names contain the process id, e.g. `/cc_shm_1234`),
- only objects with the prefixes `cc_shm_` / `cc_sem_` are ever touched,
- if you press Ctrl-C (or send SIGTERM) during `ipc`, it releases its shared memory and semaphores *before* stopping.
  `SIGKILL` (`kill -9`) cannot be caught, so run `./campuscompute cleanup` afterwards if that ever happens.
- `cleanup` skips objects whose creating process is still running.

The only file left behind is `campuscompute.log`, which is the program's output (it is overwritten each time `logger` runs).
Remove it with `./campuscompute cleanup --logs` or `make clean`.
To check for leftovers at any time: `./campuscompute verify` or `ls /dev/shm`.

## Notes

- Worker failures are **simulated** (the worker calls `exit(3)` or sends itself `SIGKILL`). They are labelled as simulated in the output and are not real OS faults.
- `rlimit cpu` takes about 1 second (it burns 1 second of CPU time until the kernel stops it).
- Files: `main.c` (CLI), `jobs.c`, `sched.c`, `logger.c`, `ipc.c`, `banker.c`, `worker.c`, `limits.c`, `verify.c`, `demo.c`, `util.c`, `campuscompute.h`.
