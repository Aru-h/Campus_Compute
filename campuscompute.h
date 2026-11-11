/*
 * campuscompute.h - shared types and function declarations
 *
 * CampusCompute: a command-line prototype of an OS job scheduler,
 * activity logger, IPC demo, Banker's algorithm, worker monitor and
 * resource-limit demo.  Written in plain C for Linux / WSL Ubuntu.
 *
 * Every module exposes one "cmd_xxx" function that main() calls.
 * argv[0] of that function is the sub-command name (e.g. "sched").
 */
#ifndef CAMPUSCOMPUTE_H
#define CAMPUSCOMPUTE_H

#define PROGRAM_NAME "campuscompute"

/* ---------- limits (kept small so arrays are easy to understand) ---------- */
#define MAX_JOBS      20      /* max jobs in one job set                    */
#define MAX_BURST     100     /* max CPU burst of one job (time units)      */
#define MAX_SEGMENTS  2500    /* max boxes in a Gantt chart                 */
#define MAX_PROC      10      /* Banker's: max processes                    */
#define MAX_RES       5       /* Banker's: max resource types               */

/* ---------- FR1: jobs ---------- */

/* Life cycle of a job: NEW -> READY -> RUNNING -> DONE */
#define JOB_NEW      0
#define JOB_READY    1
#define JOB_RUNNING  2
#define JOB_DONE     3

struct Job {
    int id;           /* job id (positive, unique)                          */
    int arrival;      /* time the job enters the system                     */
    int burst;        /* CPU time the job needs                             */
    int priority;     /* smaller number = higher priority                   */
    int status;       /* JOB_NEW ... JOB_DONE                               */

    /* filled in by the scheduler */
    int first_start;  /* first time the job got the CPU                     */
    int completion;   /* time the job finished                              */
    int turnaround;   /* completion - arrival                               */
    int waiting;      /* turnaround - burst                                 */
    int response;     /* first_start - arrival                              */
};

struct JobSet {
    struct Job jobs[MAX_JOBS];
    int count;
};

const char *status_name(int status);
void jobs_load_default(struct JobSet *set);
int  jobs_load_file(struct JobSet *set, const char *path);   /* 0 ok, -1 error */
void jobs_print(const struct JobSet *set);

/* ---------- FR2: scheduling ---------- */

#define POLICY_FCFS      0
#define POLICY_RR        1
#define POLICY_PRIORITY  2

/* One box of the Gantt chart.  job_id == -1 means the CPU was idle. */
struct Segment {
    int job_id;
    int start;
    int end;
};

struct SchedResult {
    int policy;
    int quantum;                       /* only used by Round Robin          */
    struct JobSet set;                 /* copy of the jobs with results     */
    struct Segment seg[MAX_SEGMENTS];  /* the Gantt chart                   */
    int nseg;
    double avg_completion;
    double avg_turnaround;
    double avg_waiting;
    double avg_response;
    int makespan;                      /* time the last job finished        */
    int dispatches;                    /* how many times a job got the CPU  */
};

int  run_scheduler(int policy, const struct JobSet *set, int quantum,
                   struct SchedResult *res);
void print_result(const struct SchedResult *res, int verbose);
int  cmd_sched(int argc, char **argv);

/* ---------- FR3: logger (threads) ---------- */
int cmd_logger(int argc, char **argv);

/* ---------- FR4: shared memory + semaphore ---------- */
int cmd_ipc(int argc, char **argv);

/* ---------- FR5: Banker's algorithm ---------- */
int cmd_banker(int argc, char **argv);

/* ---------- FR6: worker processes ---------- */
int cmd_worker(int argc, char **argv);

/* ---------- FR7: resource limits + cleanup ---------- */
int cmd_rlimit(int argc, char **argv);
int cmd_verify(int argc, char **argv);
int cmd_cleanup(int argc, char **argv);

/* ---------- integrated demo ---------- */
int cmd_demo(int argc, char **argv);

/* ---------- helpers (util.c) ---------- */
int  parse_number(const char *text, long min, long max, long *out);
void print_line(char ch, int count);

#endif
