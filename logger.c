/*
 * logger.c - FR3: concurrent activity logger (POSIX threads)
 *
 * The classic BOUNDED BUFFER (producer-consumer) problem:
 *
 *   producer threads  --->  [ circular queue of fixed size ]  --->  consumer threads
 *   (lab events)                                                     (write log file)
 *
 * Shared data  : the queue, its counters and the log file.
 * Mutex        : only one thread may touch the shared data at a time
 *                (prevents a race condition).
 * not_full     : producers WAIT on this when the queue is full.
 * not_empty    : consumers WAIT on this when the queue is empty.
 *
 * Why "while" and not "if" around pthread_cond_wait?  A thread can wake up
 * and find the condition false again (another thread got there first, or a
 * spurious wake-up), so it must re-check the condition in a loop.
 *
 * The consumer writes to the log file while still holding the mutex.  That
 * keeps the lines in the same order as the sequence numbers and also
 * protects the FILE pointer - two consumers can never write at once.
 *
 * Clean termination: after all producers are joined, main sets
 * producers_done = 1 and wakes the consumers.  A consumer exits only when
 * the queue is empty AND producers_done is set, so nothing is lost.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "campuscompute.h"

#define MAX_QUEUE      32
#define MAX_PRODUCERS  8
#define MAX_CONSUMERS  4
#define TEXT_LEN       100

struct LogEntry {
    int  seq;                 /* sequence number, 1,2,3,... in queue order */
    int  producer;            /* which producer made it                    */
    char text[TEXT_LEN];      /* the event text                            */
    struct timespec when;     /* time the event was created                */
};

/* ---- shared data (protected by "lock") ---- */
static struct LogEntry queue[MAX_QUEUE];
static int queue_size;                    /* capacity chosen by the user  */
static int head, tail, count;             /* circular queue indexes       */
static int next_seq;
static int producers_done;
static FILE *log_file;

static long produced, consumed;           /* statistics                   */
static long full_waits, empty_waits;

static pthread_mutex_t lock      = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  not_full  = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;

/* ---- settings (read-only while threads run) ---- */
static int items_per_producer;
static int producer_delay_us = 2000;      /* producers are fast ...       */
static int consumer_delay_us = 8000;      /* ... consumers are slow, so the queue fills up */

static const char *event_names[] = {
    "job submitted", "job started", "job finished", "quota warning", "user login"
};

/* Producer thread: creates events and puts them in the queue. */
static void *producer_thread(void *arg)
{
    int id = *(int *)arg;
    int i;

    for (i = 1; i <= items_per_producer; i++) {
        struct LogEntry e;

        e.producer = id;
        snprintf(e.text, sizeof(e.text), "%s (event %d of %d)",
                 event_names[(id + i) % 5], i, items_per_producer);
        clock_gettime(CLOCK_REALTIME, &e.when);

        pthread_mutex_lock(&lock);                 /* enter critical section */
        while (count == queue_size) {              /* queue FULL: wait       */
            full_waits++;
            printf("  [producer %d] queue FULL (%d/%d) - waiting\n", id, count, queue_size);
            pthread_cond_wait(&not_full, &lock);   /* releases lock while waiting */
        }
        e.seq = ++next_seq;
        queue[tail] = e;
        tail = (tail + 1) % queue_size;
        count++;
        produced++;
        printf("  [producer %d] put   seq %-2d  queue %d/%d\n", id, e.seq, count, queue_size);
        pthread_cond_signal(&not_empty);           /* wake a consumer        */
        pthread_mutex_unlock(&lock);               /* leave critical section */

        usleep(producer_delay_us * id);            /* stagger the producers  */
    }
    return NULL;
}

/* Consumer thread: takes events from the queue and writes them to the log file. */
static void *consumer_thread(void *arg)
{
    int id = *(int *)arg;

    for (;;) {
        struct LogEntry e;
        struct tm tm;
        char clock_text[32];

        pthread_mutex_lock(&lock);
        while (count == 0 && !producers_done) {    /* queue EMPTY: wait      */
            empty_waits++;
            printf("  [consumer %d] queue EMPTY - waiting\n", id);
            pthread_cond_wait(&not_empty, &lock);
        }
        if (count == 0 && producers_done) {        /* nothing left: finish   */
            pthread_mutex_unlock(&lock);
            break;
        }

        e = queue[head];
        head = (head + 1) % queue_size;
        count--;
        consumed++;
        printf("  [consumer %d] took  seq %-2d  queue %d/%d\n", id, e.seq, count, queue_size);

        /* write the log line (still holding the mutex, see header comment) */
        localtime_r(&e.when.tv_sec, &tm);
        strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &tm);
        fprintf(log_file, "[%04d] %s.%03ld PRODUCER-%d  %s  (logged by consumer %d)\n",
                e.seq, clock_text, e.when.tv_nsec / 1000000, e.producer, e.text, id);
        fflush(log_file);

        pthread_cond_signal(&not_full);            /* a slot is free         */
        pthread_mutex_unlock(&lock);

        usleep(consumer_delay_us);                 /* pretend writing is slow */
    }
    return NULL;
}

/* Read the log file back: count lines, check the sequence numbers are 1,2,3,... */
static int check_log(const char *path, long expected, int show_lines)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    long lines = 0;
    int in_order = 1;

    if (fp == NULL) {
        perror("cannot re-open log file");
        return -1;
    }
    printf("\n---- contents of %s ----\n", path);
    while (fgets(line, sizeof(line), fp) != NULL) {
        int seq = 0;
        lines++;
        if (sscanf(line, "[%d]", &seq) != 1 || seq != lines)
            in_order = 0;
        if (lines <= show_lines)
            fputs(line, stdout);
    }
    if (lines > show_lines)
        printf("... (%ld more lines)\n", lines - show_lines);
    fclose(fp);

    printf("---- end of log ----\n");
    printf("Log check: %ld lines written, %ld expected, sequence numbers in order: %s\n",
           lines, expected, in_order ? "yes" : "NO");
    return (lines == expected && in_order) ? 0 : -1;
}

static void logger_usage(void)
{
    fprintf(stderr,
            "usage: %s logger [-p producers 1..%d] [-c consumers 1..%d] [-n items 1..50]\n"
            "                 [-s queue_size 1..%d] [-o logfile]\n"
            "defaults: -p 3 -c 2 -n 4 -s 3 -o campuscompute.log\n",
            PROGRAM_NAME, MAX_PRODUCERS, MAX_CONSUMERS, MAX_QUEUE);
}

int cmd_logger(int argc, char **argv)
{
    long producers = 3, consumers = 2, items = 4, qsize = 3;
    const char *path = "campuscompute.log";
    pthread_t ptid[MAX_PRODUCERS], ctid[MAX_CONSUMERS];
    int pids[MAX_PRODUCERS], cids[MAX_CONSUMERS];
    int i, err;
    int created_p = 0, created_c = 0;
    int result = 0;

    /* ---- read and validate options ---- */
    for (i = 1; i < argc; i++) {
        if (i + 1 >= argc) {
            fprintf(stderr, "error: option '%s' needs a value\n", argv[i]);
            logger_usage();
            return 2;
        }
        if (strcmp(argv[i], "-p") == 0) {
            if (parse_number(argv[++i], 1, MAX_PRODUCERS, &producers) != 0) {
                fprintf(stderr, "error: producers must be 1..%d\n", MAX_PRODUCERS);
                return 2;
            }
        } else if (strcmp(argv[i], "-c") == 0) {
            if (parse_number(argv[++i], 1, MAX_CONSUMERS, &consumers) != 0) {
                fprintf(stderr, "error: consumers must be 1..%d\n", MAX_CONSUMERS);
                return 2;
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            if (parse_number(argv[++i], 1, 50, &items) != 0) {
                fprintf(stderr, "error: items must be 1..50\n");
                return 2;
            }
        } else if (strcmp(argv[i], "-s") == 0) {
            if (parse_number(argv[++i], 1, MAX_QUEUE, &qsize) != 0) {
                fprintf(stderr, "error: queue size must be 1..%d\n", MAX_QUEUE);
                return 2;
            }
        } else if (strcmp(argv[i], "-o") == 0) {
            path = argv[++i];
        } else {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            logger_usage();
            return 2;
        }
    }

    /* ---- reset shared state (the demo may call this more than once) ---- */
    queue_size = (int)qsize;
    items_per_producer = (int)items;
    head = tail = count = 0;
    next_seq = 0;
    producers_done = 0;
    produced = consumed = full_waits = empty_waits = 0;

    log_file = fopen(path, "w");
    if (log_file == NULL) {
        perror("error: cannot create log file");
        return 1;
    }

    printf("Logger: %ld producers, %ld consumers, %ld events each, queue size %ld, log file '%s'\n\n",
           producers, consumers, items, qsize, path);

    /* ---- start the consumer threads first, then the producers ---- */
    for (i = 0; i < consumers; i++) {
        cids[i] = i;
        err = pthread_create(&ctid[i], NULL, consumer_thread, &cids[i]);
        if (err != 0) {
            fprintf(stderr, "error: pthread_create (consumer): %s\n", strerror(err));
            result = 1;
            break;
        }
        created_c++;
    }
    for (i = 0; result == 0 && i < producers; i++) {
        pids[i] = i + 1;                       /* producer ids start at 1 */
        err = pthread_create(&ptid[i], NULL, producer_thread, &pids[i]);
        if (err != 0) {
            fprintf(stderr, "error: pthread_create (producer): %s\n", strerror(err));
            result = 1;
            break;
        }
        created_p++;
    }

    /* ---- orderly shutdown: join producers, tell consumers, join consumers ---- */
    for (i = 0; i < created_p; i++)
        pthread_join(ptid[i], NULL);
    printf("\nAll %d producer threads finished (joined).\n", created_p);

    pthread_mutex_lock(&lock);
    producers_done = 1;                        /* no more events will come */
    pthread_cond_broadcast(&not_empty);        /* wake every sleeping consumer */
    pthread_mutex_unlock(&lock);

    for (i = 0; i < created_c; i++)
        pthread_join(ctid[i], NULL);
    printf("All %d consumer threads finished (joined).\n", created_c);

    fclose(log_file);
    log_file = NULL;

    printf("\nStatistics: produced %ld, consumed %ld, producer waits (queue full) %ld, "
           "consumer waits (queue empty) %ld\n", produced, consumed, full_waits, empty_waits);
    if (produced != consumed || produced != producers * items) {
        printf("ERROR: produced and consumed counts do not match\n");
        result = 1;
    } else {
        printf("Synchronization check: every event produced was consumed exactly once - OK\n");
    }

    if (check_log(path, produced, 30) != 0)
        result = 1;
    return result;
}
