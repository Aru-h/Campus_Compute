/*
 * worker.c - FR6: worker processes and failure detection
 *
 * The parent plays the "scheduler daemon".  It starts worker processes with
 * fork() and later collects how each one ended with waitpid().
 *
 * fork() returns:   0 in the CHILD,   the child's pid in the PARENT,   -1 on error.
 *
 * waitpid() fills in a "status" number.  We decode it with macros:
 *   WIFEXITED(status)    true if the child called exit()/returned
 *   WEXITSTATUS(status)  the exit code (0 = success, non-zero = failure)
 *   WIFSIGNALED(status)  true if the child was killed by a signal
 *   WTERMSIG(status)     which signal killed it
 *
 * Collecting the status is also what removes the child from the process
 * table.  A child that has ended but has not been waited for is a ZOMBIE.
 *
 * IMPORTANT: the failures here are SIMULATED on purpose.  The worker itself
 * calls exit(3) or sends itself SIGKILL.  They are not real operating
 * system faults, and the output says so.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "campuscompute.h"

#define MODE_NORMAL 0
#define MODE_FAIL   1
#define MODE_CRASH  2

#define SIMULATED_EXIT_CODE 3

static const char *mode_name(int mode)
{
    if (mode == MODE_NORMAL)
        return "normal";
    if (mode == MODE_FAIL)
        return "simulated failure (exit code)";
    return "simulated crash (killed by signal)";
}

/* What a worker does.  It never returns: it ends the child process. */
static void worker_main(int number, int mode)
{
    volatile long sum = 0;       /* volatile: stops the compiler removing the loop */
    long i;

    printf("  [worker %d, pid %d] started (%s)\n", number, (int)getpid(), mode_name(mode));

    for (i = 1; i <= 2000000; i++)          /* a small amount of CPU work */
        sum += i % 7;

    if (mode == MODE_FAIL) {
        printf("  [worker %d, pid %d] SIMULATED FAILURE: exiting with code %d (not a real OS error)\n",
               number, (int)getpid(), SIMULATED_EXIT_CODE);
        _exit(SIMULATED_EXIT_CODE);          /* _exit in a child: do not flush the parent's buffers twice */
    }
    if (mode == MODE_CRASH) {
        printf("  [worker %d, pid %d] SIMULATED CRASH: sending myself SIGKILL (not a real OS error)\n",
               number, (int)getpid());
        kill(getpid(), SIGKILL);             /* process dies here */
    }

    printf("  [worker %d, pid %d] finished its work (result %ld)\n", number, (int)getpid(), (long)sum);
    _exit(0);
}

/* Run a list of workers (one per entry of modes[]) and report how each ended. */
static int run_workers(const int *modes, int count)
{
    pid_t pids[3];
    int reaped[3] = {0};
    int ok = 1;
    int remaining = count;
    int i;

    fflush(stdout);                          /* avoid duplicated buffered text in the children */
    for (i = 0; i < count; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("error: fork");
            ok = 0;
            remaining--;                     /* this worker never started */
            reaped[i] = 1;
            continue;
        }
        if (pids[i] == 0)
            worker_main(i + 1, modes[i]);    /* child: never returns */
        /* parent continues the loop and starts the next worker */
    }

    printf("\nParent (pid %d) started %d worker(s) and now waits for them with waitpid()\n\n",
           (int)getpid(), remaining);

    while (remaining > 0) {
        int status;
        pid_t done = waitpid(-1, &status, 0);       /* -1: wait for any child */

        if (done < 0) {
            if (errno == EINTR)
                continue;
            perror("error: waitpid");
            return 1;
        }

        for (i = 0; i < count; i++) {
            if (pids[i] != done || reaped[i])
                continue;
            reaped[i] = 1;
            remaining--;

            printf("  [parent] worker %d (pid %d): ", i + 1, (int)done);
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("exited normally, code 0 -> SUCCESS\n");
                if (modes[i] != MODE_NORMAL)
                    ok = 0;                  /* a failing worker looked fine: detection is wrong */
            } else if (WIFEXITED(status)) {
                printf("exited with NON-ZERO code %d -> FAILURE DETECTED\n", WEXITSTATUS(status));
                if (modes[i] != MODE_FAIL || WEXITSTATUS(status) != SIMULATED_EXIT_CODE)
                    ok = 0;
            } else if (WIFSIGNALED(status)) {
                printf("killed by signal %d (%s) -> ABNORMAL TERMINATION DETECTED\n",
                       WTERMSIG(status), strsignal(WTERMSIG(status)));
                if (modes[i] != MODE_CRASH)
                    ok = 0;
            } else {
                printf("unknown status 0x%x\n", status);
                ok = 0;
            }
        }
    }

    printf("\nAll workers collected; no zombie processes remain.\n");
    printf("Detection result: %s\n", ok ? "parent's report matches what each worker did - OK"
                                          : "MISMATCH between report and expected behaviour");
    return ok ? 0 : 1;
}

int cmd_worker(int argc, char **argv)
{
    int modes[3];

    if (argc != 2) {
        fprintf(stderr, "usage: %s worker normal | fail | crash | all\n", PROGRAM_NAME);
        return 2;
    }

    if (strcmp(argv[1], "normal") == 0) {
        modes[0] = MODE_NORMAL;
        return run_workers(modes, 1);
    }
    if (strcmp(argv[1], "fail") == 0) {
        modes[0] = MODE_FAIL;
        return run_workers(modes, 1);
    }
    if (strcmp(argv[1], "crash") == 0) {
        modes[0] = MODE_CRASH;
        return run_workers(modes, 1);
    }
    if (strcmp(argv[1], "all") == 0) {
        modes[0] = MODE_NORMAL;
        modes[1] = MODE_FAIL;
        modes[2] = MODE_CRASH;
        return run_workers(modes, 3);
    }

    fprintf(stderr, "error: unknown worker mode '%s'\n", argv[1]);
    fprintf(stderr, "usage: %s worker normal | fail | crash | all\n", PROGRAM_NAME);
    return 2;
}
