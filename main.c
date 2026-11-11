/*
 * main.c - FR8: command-line interface
 *
 * Usage:  ./campuscompute <command> [options]
 * Every command can be run on its own, so every feature is independently testable.
 */
#include <stdio.h>
#include <string.h>

#include "campuscompute.h"

static void print_help(void)
{
    printf(
"CampusCompute - intelligent job scheduling and resource management prototype\n"
"\n"
"Usage: %s <command> [options]\n"
"\n"
"Commands:\n"
"  sched fcfs|rr|priority|compare [-f jobfile] [-q quantum] [-v]\n"
"                   FR2  CPU scheduling (compare runs all three on one job set)\n"
"  logger [-p producers] [-c consumers] [-n items] [-s queue_size] [-o logfile]\n"
"                   FR3  threaded producer-consumer activity logger\n"
"  ipc [-f jobfile] FR4  shared memory + named semaphore between two processes\n"
"  banker safe|unsafe|-f file\n"
"                   FR5  Banker's safety algorithm (built-in safe / unsafe cases)\n"
"  worker normal|fail|crash|all\n"
"                   FR6  child processes: fork() + waitpid() failure detection\n"
"  rlimit [nofile|mem|cpu|all]\n"
"                   FR7  resource limits with setrlimit()\n"
"  verify           FR7  check that no child / thread / IPC object is left over\n"
"  cleanup [--logs] FR7  remove leftover IPC objects made by this program\n"
"  demo             run every feature once and print a summary\n"
"  help             show this message\n"
"\n"
"Job file format (one job per line):  id  arrival  burst  priority\n"
"Examples:\n"
"  %s sched rr -q 2\n"
"  %s sched compare -f data/jobs.txt\n"
"  %s banker unsafe\n"
"  %s worker fail\n",
        PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME, PROGRAM_NAME);
}

int main(int argc, char **argv)
{
    const char *cmd;

    /* Line-buffered output so text from child processes and threads
       appears in the right order even when output goes to a file or pipe. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (argc < 2) {
        fprintf(stderr, "error: no command given\n\n");
        print_help();
        return 2;
    }

    cmd = argv[1];

    /* argv + 1 so each command sees its own name as argv[0] */
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_help();
        return 0;
    }
    if (strcmp(cmd, "sched") == 0)
        return cmd_sched(argc - 1, argv + 1);
    if (strcmp(cmd, "logger") == 0)
        return cmd_logger(argc - 1, argv + 1);
    if (strcmp(cmd, "ipc") == 0)
        return cmd_ipc(argc - 1, argv + 1);
    if (strcmp(cmd, "banker") == 0)
        return cmd_banker(argc - 1, argv + 1);
    if (strcmp(cmd, "worker") == 0)
        return cmd_worker(argc - 1, argv + 1);
    if (strcmp(cmd, "rlimit") == 0)
        return cmd_rlimit(argc - 1, argv + 1);
    if (strcmp(cmd, "verify") == 0)
        return cmd_verify(argc - 1, argv + 1);
    if (strcmp(cmd, "cleanup") == 0)
        return cmd_cleanup(argc - 1, argv + 1);
    if (strcmp(cmd, "demo") == 0)
        return cmd_demo(argc - 1, argv + 1);

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    fprintf(stderr, "Run '%s help' to see the list of commands.\n", PROGRAM_NAME);
    return 2;
}
