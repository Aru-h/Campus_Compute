/*
 * menu.c - FR8: interactive menu (an alternative to the sub-commands)
 *
 * Start it with:  ./campuscompute menu
 *
 * The menu lets the user type in jobs, load them from a file, pick a
 * scheduling algorithm, and run every other feature.  It does NOT contain
 * any new algorithm: each menu item calls the same functions that the
 * command-line sub-commands call (cmd_logger, cmd_banker, ...), so the two
 * ways of using the program always give the same results.
 *
 * Input handling:
 *   - every line is read with fgets() and converted with parse_number(),
 *     so letters, empty lines and out-of-range numbers are rejected with a
 *     message and the question is asked again;
 *   - if the input ends (Ctrl-D, or a pipe/file that runs out) the menu
 *     stops cleanly instead of looping forever.
 */
#include <stdio.h>
#include <string.h>

#include "campuscompute.h"

#define LINE_LEN        128
#define DEFAULT_QUANTUM 3
#define LAST_CHOICE     17

/* The job set used by menu items 4-8.  It starts as the built-in sample. */
static struct JobSet current;
static char current_source[LINE_LEN + 16] = "built-in sample set";   /* room for "file " + a file name */

/* ------------------------------------------------------------------ */
/* reading input                                                      */
/* ------------------------------------------------------------------ */

/*
 * Show a prompt and read one line into buf (without the newline and without
 * trailing spaces).  Returns 0 if a line was read, -1 if the input has ended.
 */
static int read_line(const char *prompt, char *buf, size_t size)
{
    size_t len;

    printf("%s", prompt);
    fflush(stdout);                         /* make sure the prompt is visible */

    if (fgets(buf, (int)size, stdin) == NULL)
        return -1;                          /* end of input (Ctrl-D) */

    if (strchr(buf, '\n') == NULL) {        /* line was longer than the buffer: */
        int c;                              /* throw the rest of it away        */
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }

    buf[strcspn(buf, "\r\n")] = '\0';
    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t'))
        buf[--len] = '\0';
    return 0;
}

/*
 * Ask for a whole number between min and max.  Keeps asking until the answer
 * is valid.  If use_default is 1, pressing Enter gives "def".
 * Returns 0 and stores the value in *out, or -1 if the input ended.
 */
static int ask_number(const char *prompt, long min, long max,
                      int use_default, long def, long *out)
{
    char line[LINE_LEN];

    for (;;) {
        if (read_line(prompt, line, sizeof(line)) != 0)
            return -1;
        if (line[0] == '\0' && use_default) {
            *out = def;
            return 0;
        }
        if (parse_number(line, min, max, out) == 0)
            return 0;
        printf("  invalid input: please type a whole number from %ld to %ld\n", min, max);
    }
}

/* ------------------------------------------------------------------ */
/* job set (FR1)                                                      */
/* ------------------------------------------------------------------ */

/* Menu 1: type the jobs in by hand.  Returns 0, or -1 if the input ended. */
static int enter_jobs(void)
{
    struct JobSet fresh;
    long count, id, arrival, burst, priority;
    int i;

    fresh.count = 0;
    if (ask_number("How many jobs? [1-20]: ", 1, MAX_JOBS, 0, 0, &count) != 0)
        return -1;

    for (i = 0; i < count; i++) {
        printf("Job %d of %ld\n", i + 1, count);
        for (;;) {
            if (ask_number("  job id (positive number): ", 1, 100000, 0, 0, &id) != 0)
                return -1;
            if (ask_number("  arrival time [0-1000]: ", 0, 1000, 0, 0, &arrival) != 0)
                return -1;
            if (ask_number("  CPU burst [1-100]: ", 1, MAX_BURST, 0, 0, &burst) != 0)
                return -1;
            if (ask_number("  priority [1-99, smaller = more important]: ", 1, 99, 0, 0, &priority) != 0)
                return -1;

            /* the ranges above are already checked; jobs_add also rejects a repeated id */
            if (jobs_add(&fresh, (int)id, (int)arrival, (int)burst, (int)priority) == 0)
                break;
            printf("  that job was not accepted - please enter it again\n");
        }
    }

    current = fresh;
    snprintf(current_source, sizeof(current_source), "typed in by hand");
    printf("\n");
    jobs_print(&current);
    return 0;
}

/* Menu 2: load the jobs from a file. */
static int load_jobs(void)
{
    char path[LINE_LEN];
    struct JobSet fresh;

    if (read_line("File name: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0') {
        printf("  no file name given\n");
        return 0;
    }
    if (jobs_load_file(&fresh, path) != 0) {
        printf("  the job set was not changed\n");     /* the error was printed already */
        return 0;
    }

    current = fresh;
    snprintf(current_source, sizeof(current_source), "file %s", path);
    printf("\n");
    jobs_print(&current);
    return 0;
}

/* ------------------------------------------------------------------ */
/* running things                                                     */
/* ------------------------------------------------------------------ */

/* Menu 5-7: run one scheduling policy on the current jobs. */
static int run_policy(int policy)
{
    struct SchedResult res;
    long quantum = DEFAULT_QUANTUM;

    if (policy == POLICY_RR &&
        ask_number("Time quantum [1-50, Enter = 3]: ", 1, 50, 1, DEFAULT_QUANTUM, &quantum) != 0)
        return -1;

    printf("\n");
    jobs_print(&current);
    printf("\n");
    run_scheduler(policy, &current, (int)quantum, &res);
    print_result(&res, 1);                  /* 1 = also show each job's life cycle */
    return 0;
}

/* Menu 8: compare the three policies. */
static int run_compare(void)
{
    long quantum;

    if (ask_number("Round Robin time quantum [1-50, Enter = 3]: ", 1, 50, 1, DEFAULT_QUANTUM, &quantum) != 0)
        return -1;
    printf("\n");
    jobs_print(&current);
    printf("\n");
    print_comparison(&current, (int)quantum);
    return 0;
}

/* Menu 9: the threaded logger, with settings chosen by the user. */
static int run_logger(void)
{
    long producers, consumers, items, qsize;
    char p[16], c[16], n[16], s[16];
    char *args[9];

    printf("Logger settings (press Enter to accept the value in brackets)\n");
    if (ask_number("  producer threads [1-8, 3]: ", 1, 8, 1, 3, &producers) != 0 ||
        ask_number("  consumer threads [1-4, 2]: ", 1, 4, 1, 2, &consumers) != 0 ||
        ask_number("  events per producer [1-50, 3]: ", 1, 50, 1, 3, &items) != 0 ||
        ask_number("  queue size [1-32, 3]: ", 1, 32, 1, 3, &qsize) != 0)
        return -1;

    snprintf(p, sizeof(p), "%ld", producers);
    snprintf(c, sizeof(c), "%ld", consumers);
    snprintf(n, sizeof(n), "%ld", items);
    snprintf(s, sizeof(s), "%ld", qsize);
    args[0] = "logger";
    args[1] = "-p"; args[2] = p;
    args[3] = "-c"; args[4] = c;
    args[5] = "-n"; args[6] = n;
    args[7] = "-s"; args[8] = s;

    printf("\n");
    cmd_logger(9, args);
    return 0;
}

/* Menu 13: Banker's algorithm on a file chosen by the user. */
static int run_banker_file(void)
{
    char path[LINE_LEN];
    char *args[3];

    if (read_line("Banker's input file: ", path, sizeof(path)) != 0)
        return -1;
    if (path[0] == '\0') {
        printf("  no file name given\n");
        return 0;
    }
    args[0] = "banker";
    args[1] = "-f";
    args[2] = path;
    printf("\n");
    cmd_banker(3, args);
    return 0;
}

/*
 * Menu 14 and 15: ask for a mode word (e.g. normal/fail/crash/all) and call
 * the command.  An unknown word is reported by the command itself.
 */
static int run_with_mode(const char *prompt, const char *command,
                         int (*run)(int, char **))
{
    char mode[LINE_LEN];
    char *args[2];

    if (read_line(prompt, mode, sizeof(mode)) != 0)
        return -1;
    if (mode[0] == '\0')
        snprintf(mode, sizeof(mode), "all");

    args[0] = (char *)command;
    args[1] = mode;
    printf("\n");
    run(2, args);
    return 0;
}

/* Menu 10, 11, 12, 16, 17: commands that need no further input. */
static void run_fixed(int (*run)(int, char **), const char *name, const char *arg)
{
    char *args[2];
    int argc = 1;

    args[0] = (char *)name;
    if (arg != NULL) {
        args[1] = (char *)arg;
        argc = 2;
    }
    printf("\n");
    run(argc, args);
}

/* ------------------------------------------------------------------ */
/* the menu                                                           */
/* ------------------------------------------------------------------ */

static void print_menu(void)
{
    printf("\n");
    print_line('=', 60);
    printf("CampusCompute menu      jobs: %d (%s)\n", current.count, current_source);
    print_line('=', 60);
    printf("  Jobs and scheduling (FR1, FR2)\n");
    printf("     1) Enter jobs by hand\n");
    printf("     2) Load jobs from a file\n");
    printf("     3) Use the built-in sample jobs\n");
    printf("     4) Show the current jobs\n");
    printf("     5) Run FCFS\n");
    printf("     6) Run Round Robin\n");
    printf("     7) Run Priority\n");
    printf("     8) Compare all three\n");
    printf("  Other features\n");
    printf("     9) Threaded logger (FR3)\n");
    printf("    10) Shared memory + semaphore (FR4)\n");
    printf("    11) Banker's algorithm - safe example (FR5)\n");
    printf("    12) Banker's algorithm - unsafe example (FR5)\n");
    printf("    13) Banker's algorithm - from a file (FR5)\n");
    printf("    14) Worker processes (FR6)\n");
    printf("    15) Resource limits (FR7)\n");
    printf("    16) Verify cleanup (FR7)\n");
    printf("    17) Remove leftover IPC objects (FR7)\n");
    printf("     0) Exit\n");
}

int cmd_menu(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: %s menu\n", PROGRAM_NAME);
        return 2;
    }

    jobs_load_default(&current);

    for (;;) {
        long choice;
        int ended = 0;                          /* set to 1 if the input ran out */

        print_menu();
        if (ask_number("Choose an option [0-17]: ", 0, LAST_CHOICE, 0, 0, &choice) != 0) {
            printf("\nEnd of input - leaving the menu.\n");
            return 0;
        }

        switch (choice) {
        case 0:
            printf("Goodbye.\n");
            return 0;
        case 1:
            ended = enter_jobs() != 0;
            break;
        case 2:
            ended = load_jobs() != 0;
            break;
        case 3:
            jobs_load_default(&current);
            snprintf(current_source, sizeof(current_source), "built-in sample set");
            printf("\n");
            jobs_print(&current);
            break;
        case 4:
            printf("\n");
            jobs_print(&current);
            break;
        case 5:
            ended = run_policy(POLICY_FCFS) != 0;
            break;
        case 6:
            ended = run_policy(POLICY_RR) != 0;
            break;
        case 7:
            ended = run_policy(POLICY_PRIORITY) != 0;
            break;
        case 8:
            ended = run_compare() != 0;
            break;
        case 9:
            ended = run_logger() != 0;
            break;
        case 10:
            run_fixed(cmd_ipc, "ipc", NULL);
            break;
        case 11:
            run_fixed(cmd_banker, "banker", "safe");
            break;
        case 12:
            run_fixed(cmd_banker, "banker", "unsafe");
            break;
        case 13:
            ended = run_banker_file() != 0;
            break;
        case 14:
            ended = run_with_mode("Mode [normal/fail/crash/all, Enter = all]: ",
                                  "worker", cmd_worker) != 0;
            break;
        case 15:
            ended = run_with_mode("Limit [nofile/mem/cpu/all, Enter = all]: ",
                                  "rlimit", cmd_rlimit) != 0;
            break;
        case 16:
            run_fixed(cmd_verify, "verify", NULL);
            break;
        case 17:
            run_fixed(cmd_cleanup, "cleanup", NULL);
            break;
        }

        if (ended) {
            printf("\nEnd of input - leaving the menu.\n");
            return 0;
        }
    }
}
