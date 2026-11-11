/*
 * banker.c - FR5: Banker's algorithm (safety algorithm) for deadlock avoidance
 *
 * Data (n processes, m resource types):
 *   Available[m]      - free units of each resource right now
 *   Maximum[n][m]     - the most each process may ever ask for
 *   Allocation[n][m]  - what each process currently holds
 *   Need[n][m]        - what each process may still ask for
 *                       Need = Maximum - Allocation
 *
 * SAFETY ALGORITHM:
 *   1. Work = Available;  Finish[i] = false for every process.
 *   2. Find a process i with Finish[i] == false and Need[i] <= Work.
 *      (we always scan from P0 upward, so the lowest number is chosen first)
 *   3. Pretend it runs to completion and gives everything back:
 *          Work = Work + Allocation[i];  Finish[i] = true;  add i to the sequence.
 *      Go back to step 2.
 *   4. If every process finished, the state is SAFE and the order we found is
 *      a safe sequence.  If we get stuck, the state is UNSAFE (a deadlock
 *      is possible - not certain, but the system can no longer guarantee that
 *      everybody finishes).
 *
 * Input file format (numbers only, '#' starts a comment):
 *     n m
 *     Available   (m numbers)
 *     Maximum     (n rows of m numbers)
 *     Allocation  (n rows of m numbers)
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "campuscompute.h"

struct State {
    int n, m;
    int available[MAX_RES];
    int maximum[MAX_PROC][MAX_RES];
    int allocation[MAX_PROC][MAX_RES];
    int need[MAX_PROC][MAX_RES];
};

/* Built-in SAFE example (the textbook one): 5 processes, 3 resource types. */
static const char SAFE_TEXT[] =
    "5 3\n"
    "3 3 2\n"
    "7 5 3\n 3 2 2\n 9 0 2\n 2 2 2\n 4 3 3\n"
    "0 1 0\n 2 0 0\n 3 0 2\n 2 1 1\n 0 0 2\n";

/*
 * Built-in UNSAFE example: only P0 can finish (it needs 1 0 0).  After it
 * returns its resources, Work = 1 1 0 and nobody else's Need fits, so the
 * system is stuck.
 */
static const char UNSAFE_TEXT[] =
    "4 3\n"
    "1 0 0\n"
    "1 1 0\n 3 2 0\n 1 2 2\n 2 0 3\n"
    "0 1 0\n 2 0 0\n 1 1 1\n 0 0 2\n";

/* ---------------- reading the input ---------------- */

/* Read the next integer from *p, skipping spaces/newlines/comments. Returns 0 if ok. */
static int next_int(const char **p, int *value)
{
    char *end;
    long v;

    for (;;) {
        while (isspace((unsigned char)**p))
            (*p)++;
        if (**p == '#') {                       /* comment: skip to end of line */
            while (**p != '\0' && **p != '\n')
                (*p)++;
        } else {
            break;
        }
    }
    if (**p == '\0')
        return -1;                              /* ran out of numbers */

    errno = 0;
    v = strtol(*p, &end, 10);
    if (end == *p || errno != 0 || v < 0 || v > 1000)
        return -1;                              /* not a number, or out of range */
    *p = end;
    *value = (int)v;
    return 0;
}

/* Turn the text into a State, validating everything.  Returns 0 if ok, -1 if not. */
static int parse_state(const char *text, struct State *s, const char *source)
{
    const char *p = text;
    int i, j, v;

    if (next_int(&p, &s->n) != 0 || next_int(&p, &s->m) != 0) {
        fprintf(stderr, "error: %s: expected 'n m' on the first line\n", source);
        return -1;
    }
    if (s->n < 1 || s->n > MAX_PROC || s->m < 1 || s->m > MAX_RES) {
        fprintf(stderr, "error: %s: need 1..%d processes and 1..%d resource types\n",
                source, MAX_PROC, MAX_RES);
        return -1;
    }

    for (j = 0; j < s->m; j++) {
        if (next_int(&p, &s->available[j]) != 0) {
            fprintf(stderr, "error: %s: Available vector must have %d non-negative numbers\n",
                    source, s->m);
            return -1;
        }
    }
    for (i = 0; i < s->n; i++) {
        for (j = 0; j < s->m; j++) {
            if (next_int(&p, &s->maximum[i][j]) != 0) {
                fprintf(stderr, "error: %s: Maximum matrix must be %d x %d non-negative numbers\n",
                        source, s->n, s->m);
                return -1;
            }
        }
    }
    for (i = 0; i < s->n; i++) {
        for (j = 0; j < s->m; j++) {
            if (next_int(&p, &s->allocation[i][j]) != 0) {
                fprintf(stderr, "error: %s: Allocation matrix must be %d x %d non-negative numbers\n",
                        source, s->n, s->m);
                return -1;
            }
        }
    }
    if (next_int(&p, &v) == 0) {
        fprintf(stderr, "error: %s: unexpected extra numbers at the end\n", source);
        return -1;
    }

    /* Need = Maximum - Allocation; a process cannot hold more than its maximum */
    for (i = 0; i < s->n; i++) {
        for (j = 0; j < s->m; j++) {
            s->need[i][j] = s->maximum[i][j] - s->allocation[i][j];
            if (s->need[i][j] < 0) {
                fprintf(stderr, "error: %s: P%d holds more of resource %d than its maximum\n",
                        source, i, j);
                return -1;
            }
        }
    }
    return 0;
}

/* ---------------- printing ---------------- */

static void print_vector(const int *v, int m)
{
    int j;
    printf("(");
    for (j = 0; j < m; j++)
        printf("%s%d", j ? " " : "", v[j]);
    printf(")");
}

static void print_matrix(const char *title, int a[MAX_PROC][MAX_RES], int n, int m)
{
    int i, j;
    printf("%s\n", title);
    for (i = 0; i < n; i++) {
        printf("  P%d ", i);
        for (j = 0; j < m; j++)
            printf("%3d", a[i][j]);
        printf("\n");
    }
}

/* ---------------- the algorithm ---------------- */

/* Is vector a <= vector b in every position? */
static int less_or_equal(const int *a, const int *b, int m)
{
    int j;
    for (j = 0; j < m; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

/* Run the safety algorithm.  Returns 1 if SAFE, 0 if UNSAFE. */
static int run_safety(const struct State *s)
{
    int work[MAX_RES];
    int finish[MAX_PROC] = {0};
    int sequence[MAX_PROC];
    int done = 0;
    int i, j;

    for (j = 0; j < s->m; j++)
        work[j] = s->available[j];              /* step 1: Work = Available */

    printf("Safety algorithm (always picks the lowest-numbered process that fits):\n");
    printf("  Work = Available = ");
    print_vector(work, s->m);
    printf("\n");

    while (done < s->n) {
        int found = -1;

        for (i = 0; i < s->n; i++) {            /* step 2: find a process that can finish */
            if (!finish[i] && less_or_equal(s->need[i], work, s->m)) {
                found = i;
                break;
            }
        }
        if (found == -1)
            break;                              /* nobody can finish: stuck */

        printf("  P%d can finish: Need ", found);
        print_vector(s->need[found], s->m);
        printf(" <= Work ");
        print_vector(work, s->m);
        for (j = 0; j < s->m; j++)              /* step 3: it returns its resources */
            work[j] += s->allocation[found][j];
        printf("  ->  Work = ");
        print_vector(work, s->m);
        printf("\n");

        finish[found] = 1;
        sequence[done++] = found;
    }

    printf("\n");
    if (done == s->n) {
        printf("RESULT: SAFE STATE. Safe sequence: <");
        for (i = 0; i < s->n; i++)
            printf("%sP%d", i ? ", " : "", sequence[i]);
        printf(">\n");
        return 1;
    }

    printf("  No unfinished process has Need <= Work ");
    print_vector(work, s->m);
    printf(":\n");
    for (i = 0; i < s->n; i++) {
        if (!finish[i]) {
            printf("    P%d needs ", i);
            print_vector(s->need[i], s->m);
            printf("\n");
        }
    }
    printf("RESULT: UNSAFE STATE. Only %d of %d processes can be guaranteed to finish",
           done, s->n);
    if (done > 0) {
        printf(" (order found: ");
        for (i = 0; i < done; i++)
            printf("%sP%d", i ? ", " : "", sequence[i]);
        printf(")");
    }
    printf(".\nThe remaining processes could deadlock, so a Banker's system would not allow this state.\n");
    return 0;
}

/* Parse, print the tables, run the algorithm. Returns 0 if the analysis ran. */
static int analyse(const char *text, const char *source, const char *title)
{
    struct State s;

    if (parse_state(text, &s, source) != 0)
        return 1;

    print_line('=', 60);
    printf("Banker's algorithm - %s\n", title);
    print_line('=', 60);
    printf("%d processes, %d resource types\n", s.n, s.m);
    printf("Available = ");
    print_vector(s.available, s.m);
    printf("\n");
    print_matrix("Maximum:", s.maximum, s.n, s.m);
    print_matrix("Allocation:", s.allocation, s.n, s.m);
    print_matrix("Need = Maximum - Allocation:", s.need, s.n, s.m);
    printf("\n");
    run_safety(&s);
    printf("\n");
    return 0;
}

/* Read a whole file into a malloc'ed string. */
static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "r");
    char *buf;
    long size;

    if (fp == NULL) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    rewind(fp);
    if (size < 0 || size > 100000) {
        fprintf(stderr, "error: '%s' is empty or too large\n", path);
        fclose(fp);
        return NULL;
    }
    buf = malloc(size + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    size = (long)fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

int cmd_banker(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "safe") == 0)
        return analyse(SAFE_TEXT, "built-in safe case", "SAFE example");
    if (argc == 2 && strcmp(argv[1], "unsafe") == 0)
        return analyse(UNSAFE_TEXT, "built-in unsafe case", "UNSAFE example");
    if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        char *text = read_file(argv[2]);
        int rc;
        if (text == NULL)
            return 1;
        rc = analyse(text, argv[2], argv[2]);
        free(text);
        return rc;
    }

    fprintf(stderr, "usage: %s banker safe | unsafe | -f <file>\n", PROGRAM_NAME);
    return 2;
}
