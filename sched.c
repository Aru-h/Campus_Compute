/*
 * sched.c - FR2: CPU scheduling simulation
 *
 *   FCFS      - First Come First Served (non-preemptive)
 *   RR        - Round Robin with a fixed time quantum (preemptive)
 *   PRIORITY  - Priority scheduling, non-preemptive (smaller number = higher priority)
 *
 * The simulation is a simple clock ("time") that moves forward.  Every time a
 * job gets the CPU we record a box (Segment) in the Gantt chart.
 *
 * TIE-BREAKING RULE (used everywhere):
 *     1. earlier arrival time goes first
 *     2. if still tied, the smaller job id goes first
 * For Priority scheduling the priority number is compared first, then rules 1 and 2.
 *
 * ROUND ROBIN CONVENTION: if a new job arrives at the same moment a quantum
 * expires, the NEW job is put in the ready queue BEFORE the preempted job.
 *
 * Formulas:   turnaround = completion - arrival
 *             waiting    = turnaround - burst
 *             response   = first_start - arrival
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "campuscompute.h"

#define DEFAULT_QUANTUM 3

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

/* Sorting rule: earlier arrival first, then smaller id (tie-break). */
static int compare_arrival_then_id(const void *a, const void *b)
{
    const struct Job *x = a;
    const struct Job *y = b;

    if (x->arrival != y->arrival)
        return x->arrival - y->arrival;
    return x->id - y->id;
}

/* Sort by id, used only to print the table neatly. */
static int compare_id(const void *a, const void *b)
{
    const struct Job *x = a;
    const struct Job *y = b;
    return x->id - y->id;
}

/* Add one box to the Gantt chart. */
static void add_segment(struct SchedResult *res, int job_id, int start, int end)
{
    res->seg[res->nseg].job_id = job_id;
    res->seg[res->nseg].start = start;
    res->seg[res->nseg].end = end;
    res->nseg++;
}

/* Called when a job finishes: store its completion time and compute metrics. */
static void finish_job(struct Job *j, int time)
{
    j->completion = time;
    j->turnaround = j->completion - j->arrival;
    j->waiting = j->turnaround - j->burst;
    j->response = j->first_start - j->arrival;
    j->status = JOB_DONE;
}

/* ------------------------------------------------------------------ */
/* the three algorithms (each works on res->set.jobs, sorted by arrival) */
/* ------------------------------------------------------------------ */

/* FCFS: run jobs one after another in order of arrival. */
static void simulate_fcfs(struct SchedResult *res)
{
    struct Job *job = res->set.jobs;
    int n = res->set.count;
    int time = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (time < job[i].arrival) {            /* nobody has arrived: CPU idle */
            add_segment(res, -1, time, job[i].arrival);
            time = job[i].arrival;
        }
        job[i].first_start = time;
        job[i].status = JOB_RUNNING;
        add_segment(res, job[i].id, time, time + job[i].burst);
        time += job[i].burst;                   /* runs to completion */
        finish_job(&job[i], time);
    }
}

/* Priority (non-preemptive): when the CPU is free, pick the arrived job with the best priority. */
static void simulate_priority(struct SchedResult *res)
{
    struct Job *job = res->set.jobs;
    int n = res->set.count;
    int time = 0;
    int done = 0;

    while (done < n) {
        int best = -1;
        int i;

        /* look at every job that has arrived and is not finished */
        for (i = 0; i < n; i++) {
            if (job[i].status == JOB_DONE || job[i].arrival > time)
                continue;
            if (best == -1) {
                best = i;
            } else if (job[i].priority < job[best].priority) {
                best = i;                        /* better priority */
            } else if (job[i].priority == job[best].priority) {
                /* tie: array is sorted by (arrival, id) so the earlier index
                   already wins - nothing to do */
            }
        }

        if (best == -1) {
            /* CPU idle: jump to the next arrival */
            int next = -1;
            for (i = 0; i < n; i++)
                if (job[i].status != JOB_DONE && (next == -1 || job[i].arrival < next))
                    next = job[i].arrival;
            add_segment(res, -1, time, next);
            time = next;
            continue;
        }

        job[best].first_start = time;
        job[best].status = JOB_RUNNING;
        add_segment(res, job[best].id, time, time + job[best].burst);
        time += job[best].burst;
        finish_job(&job[best], time);
        done++;
    }
}

/* Round Robin: each job runs at most "quantum" units, then goes to the back of the queue. */
static void simulate_rr(struct SchedResult *res)
{
    struct Job *job = res->set.jobs;
    int n = res->set.count;
    int remaining[MAX_JOBS];
    int queue[MAX_JOBS + 1];           /* circular queue of job indexes */
    int head = 0, tail = 0, in_queue = 0;
    int next_arrival = 0;              /* index of the next job that has not arrived yet */
    int time = 0;
    int done = 0;
    int i;

    for (i = 0; i < n; i++)
        remaining[i] = job[i].burst;

    while (done < n) {
        /* move every job that has arrived by now into the ready queue */
        while (next_arrival < n && job[next_arrival].arrival <= time) {
            queue[tail] = next_arrival;
            tail = (tail + 1) % (MAX_JOBS + 1);
            in_queue++;
            job[next_arrival].status = JOB_READY;
            next_arrival++;
        }

        if (in_queue == 0) {
            /* nothing ready: CPU idle until the next job arrives */
            add_segment(res, -1, time, job[next_arrival].arrival);
            time = job[next_arrival].arrival;
            continue;
        }

        /* take the job at the front of the queue */
        i = queue[head];
        head = (head + 1) % (MAX_JOBS + 1);
        in_queue--;

        if (job[i].first_start == -1)
            job[i].first_start = time;
        job[i].status = JOB_RUNNING;

        {
            int slice = remaining[i] < res->quantum ? remaining[i] : res->quantum;
            add_segment(res, job[i].id, time, time + slice);
            time += slice;
            remaining[i] -= slice;
        }

        /* jobs that arrived while this one was running join the queue first */
        while (next_arrival < n && job[next_arrival].arrival <= time) {
            queue[tail] = next_arrival;
            tail = (tail + 1) % (MAX_JOBS + 1);
            in_queue++;
            job[next_arrival].status = JOB_READY;
            next_arrival++;
        }

        if (remaining[i] == 0) {
            finish_job(&job[i], time);
            done++;
        } else {
            job[i].status = JOB_READY;          /* preempted: back of the queue */
            queue[tail] = i;
            tail = (tail + 1) % (MAX_JOBS + 1);
            in_queue++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* public functions                                                   */
/* ------------------------------------------------------------------ */

/*
 * Run one scheduling policy on a copy of the job set.
 * Returns 0 on success.  The result holds the Gantt chart and all metrics.
 */
int run_scheduler(int policy, const struct JobSet *set, int quantum, struct SchedResult *res)
{
    int i;

    memset(res, 0, sizeof(*res));
    res->policy = policy;
    res->quantum = quantum;
    res->set = *set;                    /* work on a copy, keep the input untouched */

    /* simulation needs jobs ordered by arrival time (tie-break: id) */
    qsort(res->set.jobs, res->set.count, sizeof(struct Job), compare_arrival_then_id);
    for (i = 0; i < res->set.count; i++) {
        res->set.jobs[i].status = JOB_NEW;
        res->set.jobs[i].first_start = -1;
    }

    if (policy == POLICY_FCFS)
        simulate_fcfs(res);
    else if (policy == POLICY_RR)
        simulate_rr(res);
    else
        simulate_priority(res);

    /* averages and counters */
    for (i = 0; i < res->set.count; i++) {
        const struct Job *j = &res->set.jobs[i];
        res->avg_completion += j->completion;
        res->avg_turnaround += j->turnaround;
        res->avg_waiting    += j->waiting;
        res->avg_response   += j->response;
        if (j->completion > res->makespan)
            res->makespan = j->completion;
    }
    res->avg_completion /= res->set.count;
    res->avg_turnaround /= res->set.count;
    res->avg_waiting    /= res->set.count;
    res->avg_response   /= res->set.count;

    for (i = 0; i < res->nseg; i++)
        if (res->seg[i].job_id != -1)
            res->dispatches++;

    /* put the table back in id order for printing */
    qsort(res->set.jobs, res->set.count, sizeof(struct Job), compare_id);
    return 0;
}

static const char *policy_title(const struct SchedResult *res, char *buf, size_t size)
{
    if (res->policy == POLICY_FCFS)
        snprintf(buf, size, "FCFS (First Come First Served)");
    else if (res->policy == POLICY_RR)
        snprintf(buf, size, "Round Robin (time quantum = %d)", res->quantum);
    else
        snprintf(buf, size, "Priority (non-preemptive, smaller number = higher priority)");
    return buf;
}

/* Print the Gantt chart, 8 boxes per row. */
static void print_gantt(const struct SchedResult *res)
{
    const int per_row = 8;
    int row, k;

    printf("Gantt chart:\n");
    for (row = 0; row < res->nseg; row += per_row) {
        int last = row + per_row;
        if (last > res->nseg)
            last = res->nseg;

        printf("  ");
        for (k = row; k < last; k++) {
            char label[16];
            if (res->seg[k].job_id == -1)
                snprintf(label, sizeof(label), " IDLE");
            else
                snprintf(label, sizeof(label), " J%d", res->seg[k].job_id);
            printf("|%-6s", label);
        }
        printf("|\n  ");
        for (k = row; k < last; k++)
            printf("%-7d", res->seg[k].start);
        printf("%d\n", res->seg[last - 1].end);
    }
}

/* Print the whole result: timeline, table, averages. */
void print_result(const struct SchedResult *res, int verbose)
{
    char title[100];
    int i, k;

    print_line('=', 72);
    printf("%s\n", policy_title(res, title, sizeof(title)));
    print_line('=', 72);
    printf("Tie-breaking rule: earlier arrival first, then smaller job id.\n");
    if (res->policy == POLICY_RR)
        printf("Time quantum: %d. A job arriving exactly when a quantum ends is queued before the preempted job.\n",
               res->quantum);
    printf("\n");

    print_gantt(res);

    printf("\nExecution order: ");
    for (k = 0; k < res->nseg; k++) {
        if (res->seg[k].job_id == -1)
            printf("[IDLE %d-%d] ", res->seg[k].start, res->seg[k].end);
        else
            printf("J%d ", res->seg[k].job_id);
    }
    printf("\n\n");

    printf("  %-4s %-8s %-6s %-5s %-6s %-11s %-11s %-8s %-9s %-6s\n",
           "Job", "Arrival", "Burst", "Prio", "Start", "Completion", "Turnaround", "Waiting",
           "Response", "Status");
    for (i = 0; i < res->set.count; i++) {
        const struct Job *j = &res->set.jobs[i];
        printf("  J%-3d %-8d %-6d %-5d %-6d %-11d %-11d %-8d %-9d %-6s\n",
               j->id, j->arrival, j->burst, j->priority, j->first_start,
               j->completion, j->turnaround, j->waiting, j->response, status_name(j->status));
    }

    printf("\nAverage completion time : %.2f\n", res->avg_completion);
    printf("Average turnaround time : %.2f\n", res->avg_turnaround);
    printf("Average waiting time    : %.2f\n", res->avg_waiting);
    printf("Average response time   : %.2f\n", res->avg_response);
    printf("Makespan (last finish)  : %d\n", res->makespan);

    if (verbose) {
        /* life cycle of each job, rebuilt from the Gantt chart */
        printf("\nJob life cycle (NEW -> READY -> RUNNING -> DONE):\n");
        for (i = 0; i < res->set.count; i++) {
            const struct Job *j = &res->set.jobs[i];
            printf("  J%d: READY at %d", j->id, j->arrival);
            for (k = 0; k < res->nseg; k++)
                if (res->seg[k].job_id == j->id)
                    printf(", RUNNING %d-%d", res->seg[k].start, res->seg[k].end);
            printf(", DONE at %d\n", j->completion);
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/* command line:  sched fcfs|rr|priority|compare [-f FILE] [-q N] [-v] */
/* ------------------------------------------------------------------ */

static void sched_usage(void)
{
    fprintf(stderr,
            "usage: %s sched <fcfs|rr|priority|compare> [-f jobfile] [-q quantum] [-v]\n"
            "  -f jobfile  read jobs from a file (default: built-in sample set)\n"
            "  -q quantum  Round Robin time quantum, 1..50 (default %d)\n"
            "  -v          also show each job's life cycle\n",
            PROGRAM_NAME, DEFAULT_QUANTUM);
}

int cmd_sched(int argc, char **argv)
{
    struct JobSet set;
    struct SchedResult res;
    const char *file = NULL;
    const char *which;
    long quantum = DEFAULT_QUANTUM;
    int verbose = 0;
    int i;

    if (argc < 2) {
        sched_usage();
        return 2;
    }
    which = argv[1];
    if (strcmp(which, "fcfs") != 0 && strcmp(which, "rr") != 0 &&
        strcmp(which, "priority") != 0 && strcmp(which, "compare") != 0) {
        fprintf(stderr, "error: unknown scheduling policy '%s'\n", which);
        sched_usage();
        return 2;
    }

    /* read the options */
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            file = argv[++i];
        } else if (strcmp(argv[i], "-q") == 0 && i + 1 < argc) {
            if (parse_number(argv[++i], 1, 50, &quantum) != 0) {
                fprintf(stderr, "error: quantum must be a whole number from 1 to 50\n");
                return 2;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else {
            fprintf(stderr, "error: bad or incomplete option '%s'\n", argv[i]);
            sched_usage();
            return 2;
        }
    }

    /* load the jobs */
    if (file != NULL) {
        if (jobs_load_file(&set, file) != 0)
            return 1;
    } else {
        jobs_load_default(&set);
    }

    jobs_print(&set);
    printf("\n");

    if (strcmp(which, "compare") == 0) {
        /* run all three on the SAME job set and compare */
        struct SchedResult all[3];
        const char *names[3] = {"FCFS", "Round Robin", "Priority"};
        int best_wait = 0, best_tat = 0;

        run_scheduler(POLICY_FCFS, &set, (int)quantum, &all[0]);
        run_scheduler(POLICY_RR, &set, (int)quantum, &all[1]);
        run_scheduler(POLICY_PRIORITY, &set, (int)quantum, &all[2]);

        print_line('=', 78);
        printf("Scheduling comparison on the same job set (RR quantum = %ld)\n", quantum);
        print_line('=', 78);
        printf("  %-12s %9s %11s %9s %10s %9s %11s\n",
               "Policy", "Avg CT", "Avg TAT", "Avg WT", "Avg RT", "Makespan", "Dispatches");
        for (i = 0; i < 3; i++) {
            printf("  %-12s %9.2f %11.2f %9.2f %10.2f %9d %11d\n", names[i],
                   all[i].avg_completion, all[i].avg_turnaround, all[i].avg_waiting,
                   all[i].avg_response, all[i].makespan, all[i].dispatches);
            if (all[i].avg_waiting < all[best_wait].avg_waiting)
                best_wait = i;
            if (all[i].avg_turnaround < all[best_tat].avg_turnaround)
                best_tat = i;
        }
        printf("\n");
        for (i = 0; i < 3; i++) {
            int k;
            printf("  %-12s order: ", names[i]);
            for (k = 0; k < all[i].nseg; k++) {
                if (all[i].seg[k].job_id == -1)
                    printf("IDLE ");
                else
                    printf("J%d ", all[i].seg[k].job_id);
            }
            printf("\n");
        }
        printf("\nLowest average waiting time   : %s (%.2f)\n", names[best_wait], all[best_wait].avg_waiting);
        printf("Lowest average turnaround time: %s (%.2f)\n", names[best_tat], all[best_tat].avg_turnaround);
        printf("Note: Round Robin gives every job a turn quickly (low response time) but\n"
               "      costs more dispatches; FCFS and Priority run each job in one piece.\n\n");
        return 0;
    }

    if (strcmp(which, "fcfs") == 0)
        run_scheduler(POLICY_FCFS, &set, (int)quantum, &res);
    else if (strcmp(which, "rr") == 0)
        run_scheduler(POLICY_RR, &set, (int)quantum, &res);
    else
        run_scheduler(POLICY_PRIORITY, &set, (int)quantum, &res);

    print_result(&res, verbose);
    return 0;
}
