/*
 * jobs.c - FR1: job representation, input validation, loading jobs
 *
 * A job set can come from the built-in sample or from a text file with
 * one job per line:   id  arrival  burst  priority
 * Lines starting with '#' and blank lines are ignored.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "campuscompute.h"

/* Name of a job status, for printing. */
const char *status_name(int status)
{
    switch (status) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

/*
 * Check one job's values.  Returns 0 if fine, -1 (with a message) if not.
 * "where" is text like "line 3" or "job 2" used in the message.
 */
static int validate_job(const struct Job *j, const char *where)
{
    if (j->id <= 0) {
        fprintf(stderr, "error: %s: job id must be positive (got %d)\n", where, j->id);
        return -1;
    }
    if (j->arrival < 0 || j->arrival > 1000) {
        fprintf(stderr, "error: %s: arrival must be 0..1000 (got %d)\n", where, j->arrival);
        return -1;
    }
    if (j->burst < 1 || j->burst > MAX_BURST) {
        fprintf(stderr, "error: %s: burst must be 1..%d (got %d)\n", where, MAX_BURST, j->burst);
        return -1;
    }
    if (j->priority < 1 || j->priority > 99) {
        fprintf(stderr, "error: %s: priority must be 1..99 (got %d)\n", where, j->priority);
        return -1;
    }
    return 0;
}

/* Fill in a new job with its starting values. */
static void init_job(struct Job *j, int id, int arrival, int burst, int priority)
{
    j->id = id;
    j->arrival = arrival;
    j->burst = burst;
    j->priority = priority;
    j->status = JOB_NEW;
    j->first_start = -1;
    j->completion = 0;
    j->turnaround = 0;
    j->waiting = 0;
    j->response = 0;
}

/*
 * Built-in sample job set (same as data/jobs.txt).
 * Job 6 arrives late (t=40) so the CPU sits idle for a while - this shows
 * that the schedulers handle idle time.  Jobs 2 and 5 have the same
 * priority so the tie-breaking rule is visible.
 */
void jobs_load_default(struct JobSet *set)
{
    /* id, arrival, burst, priority */
    static const int sample[6][4] = {
        {1,  0, 8, 3},
        {2,  1, 4, 1},
        {3,  2, 9, 4},
        {4,  3, 5, 2},
        {5,  4, 2, 1},
        {6, 40, 3, 2}
    };
    int i;

    set->count = 6;
    for (i = 0; i < 6; i++)
        init_job(&set->jobs[i], sample[i][0], sample[i][1], sample[i][2], sample[i][3]);
}

/* Load jobs from a file.  Returns 0 on success, -1 on any error. */
int jobs_load_file(struct JobSet *set, const char *path)
{
    FILE *fp;
    char line[256];
    int line_no = 0;
    int i;

    fp = fopen(path, "r");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    set->count = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        char where[40];
        char *hash;
        long field[4];
        int nfields = 0;
        int too_many = 0;
        int bad_number = 0;
        char *token;

        line_no++;
        snprintf(where, sizeof(where), "line %d", line_no);

        hash = strchr(line, '#');           /* cut off comments */
        if (hash != NULL)
            *hash = '\0';

        /* skip blank lines */
        if (line[strspn(line, " \t\r\n")] == '\0')
            continue;

        /*
         * Split the line into words and convert each with parse_number().
         * (sscanf("%d") is not used because a huge number such as
         * 4294967297 would silently wrap around into a small valid value.)
         */
        for (token = strtok(line, " \t\r\n"); token != NULL; token = strtok(NULL, " \t\r\n")) {
            if (nfields == 4) {
                too_many = 1;
                break;
            }
            if (parse_number(token, -1000000, 1000000, &field[nfields]) != 0) {
                bad_number = 1;
                break;
            }
            nfields++;
        }
        if (bad_number) {
            fprintf(stderr, "error: %s: '%s' is not a valid whole number (or is too large)\n", where, token);
            fclose(fp);
            return -1;
        }
        if (too_many) {
            fprintf(stderr, "error: %s: unexpected extra text after the 4 numbers\n", where);
            fclose(fp);
            return -1;
        }
        if (nfields != 4) {
            fprintf(stderr, "error: %s: expected 4 integers 'id arrival burst priority'\n", where);
            fclose(fp);
            return -1;
        }
        if (set->count >= MAX_JOBS) {
            fprintf(stderr, "error: too many jobs (maximum is %d)\n", MAX_JOBS);
            fclose(fp);
            return -1;
        }

        init_job(&set->jobs[set->count], (int)field[0], (int)field[1], (int)field[2], (int)field[3]);
        if (validate_job(&set->jobs[set->count], where) != 0) {
            fclose(fp);
            return -1;
        }
        set->count++;
    }
    fclose(fp);

    if (set->count == 0) {
        fprintf(stderr, "error: job file '%s' contains no jobs\n", path);
        return -1;
    }

    /* job ids must be unique */
    for (i = 0; i < set->count; i++) {
        int k;
        for (k = i + 1; k < set->count; k++) {
            if (set->jobs[i].id == set->jobs[k].id) {
                fprintf(stderr, "error: duplicate job id %d\n", set->jobs[i].id);
                return -1;
            }
        }
    }
    return 0;
}

/* Print the job table (input data). */
void jobs_print(const struct JobSet *set)
{
    int i;

    printf("Job set (%d jobs)\n", set->count);
    printf("  %-4s %-8s %-6s %-9s %-8s\n", "ID", "Arrival", "Burst", "Priority", "Status");
    for (i = 0; i < set->count; i++) {
        const struct Job *j = &set->jobs[i];
        printf("  J%-3d %-8d %-6d %-9d %-8s\n",
               j->id, j->arrival, j->burst, j->priority, status_name(j->status));
    }
}
