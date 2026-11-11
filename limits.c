/*
 * limits.c - FR7: process resource limits with setrlimit()
 *
 * Every process has limits set by the kernel (see "ulimit -a").  Each limit
 * has a SOFT value (the one actually enforced) and a HARD value (the ceiling
 * the soft limit may be raised to).  getrlimit() reads them, setrlimit()
 * changes them.
 *
 * We always apply the limit inside a forked CHILD process, so the limit
 * affects only that child and the main program keeps running normally.
 *
 *   nofile : RLIMIT_NOFILE - max open file descriptors -> open() fails with EMFILE
 *   mem    : RLIMIT_AS     - max virtual memory        -> malloc() returns NULL (ENOMEM)
 *   cpu    : RLIMIT_CPU    - max CPU seconds           -> kernel sends SIGXCPU and the process dies
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "campuscompute.h"

#define OPEN_LIMIT   16                    /* file descriptors */
#define MEMORY_LIMIT (64L * 1024 * 1024)   /* 64 MiB           */
#define CPU_LIMIT    1                     /* seconds          */

/* Print the soft/hard values of one limit. */
static void show_limit(const char *label, int resource)
{
    struct rlimit rl;

    if (getrlimit(resource, &rl) != 0) {
        perror("getrlimit");
        return;
    }
    printf("    %-14s soft = ", label);
    if (rl.rlim_cur == RLIM_INFINITY)
        printf("unlimited");
    else
        printf("%lu", (unsigned long)rl.rlim_cur);
    printf(", hard = ");
    if (rl.rlim_max == RLIM_INFINITY)
        printf("unlimited\n");
    else
        printf("%lu\n", (unsigned long)rl.rlim_max);
}

/* Lower one limit (soft and hard) to "value".  Returns 0 if ok. */
static int apply_limit(int resource, unsigned long value)
{
    struct rlimit rl;

    rl.rlim_cur = value;
    rl.rlim_max = value;
    if (setrlimit(resource, &rl) != 0) {
        perror("    setrlimit");
        return -1;
    }
    return 0;
}

/* ---- child process bodies: each returns the exit code of the child ---- */

static int child_nofile(void)
{
    int opened = 0;
    int fd;
    int fds[OPEN_LIMIT + 8];

    printf("  [child] BEFORE:\n");
    show_limit("RLIMIT_NOFILE", RLIMIT_NOFILE);
    if (apply_limit(RLIMIT_NOFILE, OPEN_LIMIT) != 0)
        return 1;
    printf("  [child] AFTER setrlimit(RLIMIT_NOFILE, %d):\n", OPEN_LIMIT);
    show_limit("RLIMIT_NOFILE", RLIMIT_NOFILE);

    /* keep opening /dev/null until the kernel says no */
    for (;;) {
        if (opened >= (int)(sizeof(fds) / sizeof(fds[0])))
            break;                         /* safety stop: limit was not enforced */
        fd = open("/dev/null", O_RDONLY);
        if (fd < 0)
            break;
        fds[opened++] = fd;
    }

    printf("  [child] opened %d extra files, then open() failed: %s (errno %d)\n",
           opened, strerror(errno), errno);
    while (opened > 0)
        close(fds[--opened]);
    return (errno == EMFILE) ? 0 : 1;
}

static int child_memory(void)
{
    void *small, *big;

    printf("  [child] BEFORE:\n");
    show_limit("RLIMIT_AS", RLIMIT_AS);
    if (apply_limit(RLIMIT_AS, MEMORY_LIMIT) != 0)
        return 1;
    printf("  [child] AFTER setrlimit(RLIMIT_AS, %ld MiB):\n", MEMORY_LIMIT / (1024 * 1024));
    show_limit("RLIMIT_AS", RLIMIT_AS);

    small = malloc(8L * 1024 * 1024);        /* 8 MiB: fits under the limit */
    printf("  [child] malloc(8 MiB)   -> %s\n", small != NULL ? "succeeded" : "FAILED");

    errno = 0;
    big = malloc(256L * 1024 * 1024);        /* 256 MiB: too big */
    printf("  [child] malloc(256 MiB) -> %s", big != NULL ? "succeeded (limit NOT enforced)" : "returned NULL");
    if (big == NULL)
        printf(": %s (errno %d)", strerror(errno), errno);
    printf("\n");

    free(small);
    free(big);
    return (small != NULL && big == NULL) ? 0 : 1;
}

static int child_cpu(void)
{
    struct rlimit core = {0, 0};
    volatile unsigned long x = 0;

    setrlimit(RLIMIT_CORE, &core);           /* no core dump file when the signal kills us */

    printf("  [child] BEFORE:\n");
    show_limit("RLIMIT_CPU", RLIMIT_CPU);
    {
        struct rlimit rl;
        rl.rlim_cur = CPU_LIMIT;             /* soft limit: SIGXCPU is sent here  */
        rl.rlim_max = CPU_LIMIT + 2;         /* hard limit: SIGKILL if we ignore it */
        if (setrlimit(RLIMIT_CPU, &rl) != 0) {
            perror("    setrlimit");
            return 1;
        }
    }
    printf("  [child] AFTER setrlimit(RLIMIT_CPU, %d s): now spinning in a busy loop...\n", CPU_LIMIT);
    show_limit("RLIMIT_CPU", RLIMIT_CPU);

    for (;;)
        x++;                                 /* burn CPU until the kernel stops us */
    return 0;
}

/* Fork a child that runs one of the bodies above, wait for it, and report. */
static int run_test(const char *title, int (*body)(void), int expect_signal)
{
    pid_t pid;
    int status;
    int ok = 0;

    printf("Test: %s\n", title);
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("error: fork");
        return 1;
    }
    if (pid == 0)
        _exit(body());                       /* child */

    if (waitpid(pid, &status, 0) < 0) {
        perror("error: waitpid");
        return 1;
    }

    if (expect_signal == 0) {
        ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        printf("  [parent] child exit status: %s -> %s\n",
               WIFEXITED(status) ? "exited" : "killed", ok ? "limit had the expected effect: PASS" : "FAIL");
    } else {
        ok = WIFSIGNALED(status) && WTERMSIG(status) == expect_signal;
        if (WIFSIGNALED(status))
            printf("  [parent] child was killed by signal %d (%s) -> %s\n",
                   WTERMSIG(status), strsignal(WTERMSIG(status)),
                   ok ? "limit had the expected effect: PASS" : "FAIL (unexpected signal)");
        else
            printf("  [parent] child exited normally -> FAIL (limit had no effect)\n");
    }
    printf("\n");
    return ok ? 0 : 1;
}

int cmd_rlimit(int argc, char **argv)
{
    const char *which = "all";
    int failures = 0;

    if (argc > 2) {
        fprintf(stderr, "usage: %s rlimit [nofile|mem|cpu|all]\n", PROGRAM_NAME);
        return 2;
    }
    if (argc == 2)
        which = argv[1];

    if (strcmp(which, "nofile") != 0 && strcmp(which, "mem") != 0 &&
        strcmp(which, "cpu") != 0 && strcmp(which, "all") != 0) {
        fprintf(stderr, "error: unknown limit '%s'\n", which);
        fprintf(stderr, "usage: %s rlimit [nofile|mem|cpu|all]\n", PROGRAM_NAME);
        return 2;
    }

    printf("Resource limit demo (each limit is applied inside a child process only)\n\n");

    if (strcmp(which, "nofile") == 0 || strcmp(which, "all") == 0)
        failures += run_test("open-file limit (RLIMIT_NOFILE): open() should fail with EMFILE",
                             child_nofile, 0);
    if (strcmp(which, "mem") == 0 || strcmp(which, "all") == 0)
        failures += run_test("memory limit (RLIMIT_AS): a too-large malloc() should fail",
                             child_memory, 0);
    if (strcmp(which, "cpu") == 0 || strcmp(which, "all") == 0)
        failures += run_test("CPU-time limit (RLIMIT_CPU): busy loop should be stopped by SIGXCPU",
                             child_cpu, SIGXCPU);

    printf("Resource limit summary: %s\n", failures == 0 ? "all limits had the expected effect"
                                                         : "some tests failed");
    return failures == 0 ? 0 : 1;
}
