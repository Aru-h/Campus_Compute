/*
 * demo.c - integrated demonstration
 *
 * Runs every feature once, one after another, using the same cmd_xxx
 * functions the command line uses, then prints a PASS/FAIL summary.
 * Nothing here is hard-coded: each stage really runs and returns 0 on success.
 */
#include <stdio.h>
#include <string.h>

#include "campuscompute.h"

#define MAX_STAGES 16

static const char *stage_name[MAX_STAGES];
static int         stage_result[MAX_STAGES];
static int         stage_count;

/* Record the result of one stage. */
static void record(const char *name, int result)
{
    stage_name[stage_count] = name;
    stage_result[stage_count] = result;
    stage_count++;
}

static void banner(const char *text)
{
    printf("\n");
    print_line('#', 72);
    printf("# STAGE: %s\n", text);
    print_line('#', 72);
}

int cmd_demo(int argc, char **argv)
{
    int i, failed = 0;

    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: %s demo\n", PROGRAM_NAME);
        return 2;
    }

    printf("CampusCompute integrated demonstration\n");

    /* each array below is "argv" for one command: {command name, arguments...} */
    {
        char *fcfs[]     = {"sched", "fcfs"};
        char *rr[]       = {"sched", "rr", "-q", "3"};
        char *prio[]     = {"sched", "priority"};
        char *compare[]  = {"sched", "compare", "-q", "3"};
        char *logger[]   = {"logger", "-p", "3", "-c", "2", "-n", "3", "-s", "3"};
        char *ipc[]      = {"ipc"};
        char *safe[]     = {"banker", "safe"};
        char *unsafe[]   = {"banker", "unsafe"};
        char *workers[]  = {"worker", "all"};
        char *limits[]   = {"rlimit", "all"};
        char *verify[]   = {"verify"};

        banner("FR2  CPU scheduling - FCFS");
        record("FR2 FCFS scheduling", cmd_sched(2, fcfs));
        banner("FR2  CPU scheduling - Round Robin");
        record("FR2 Round Robin scheduling", cmd_sched(4, rr));
        banner("FR2  CPU scheduling - Priority");
        record("FR2 Priority scheduling", cmd_sched(2, prio));
        banner("FR2  Scheduling comparison");
        record("FR2 Scheduling comparison", cmd_sched(4, compare));

        banner("FR3  Threaded producer-consumer logger");
        record("FR3 Producer-consumer logger", cmd_logger(9, logger));

        banner("FR4  Shared memory + named semaphore");
        record("FR4 Shared-memory IPC", cmd_ipc(1, ipc));

        banner("FR5  Banker's algorithm - safe state");
        record("FR5 Banker's safe state", cmd_banker(2, safe));
        banner("FR5  Banker's algorithm - unsafe state");
        record("FR5 Banker's unsafe state", cmd_banker(2, unsafe));

        banner("FR6  Worker processes: normal, simulated failure, simulated crash");
        record("FR6 Worker processes", cmd_worker(2, workers));

        banner("FR7  Resource limits");
        record("FR7 Resource limits", cmd_rlimit(2, limits));

        banner("FR7  Cleanup verification");
        record("FR7 Cleanup verification", cmd_verify(1, verify));
    }

    printf("\n");
    print_line('=', 72);
    printf("DEMO SUMMARY\n");
    print_line('=', 72);
    for (i = 0; i < stage_count; i++) {
        printf("  %-34s %s\n", stage_name[i], stage_result[i] == 0 ? "PASS" : "FAIL");
        if (stage_result[i] != 0)
            failed++;
    }
    printf("\n%d of %d stages passed.\n", stage_count - failed, stage_count);
    printf("Log file written by the logger stage: campuscompute.log\n");
    return failed == 0 ? 0 : 1;
}
