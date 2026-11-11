/*
 * ipc.c - FR4: inter-process communication with POSIX shared memory and
 *         named semaphores
 *
 * Two processes (parent = "dispatcher", child = "accounting") exchange data:
 *
 *   parent  --- job id + burst, one at a time --->  child
 *   parent  <--- total CPU demand + job count ----  child
 *
 * SHARED MEMORY (shm_open + ftruncate + mmap):
 *   A block of memory that both processes can read and write.  Unlike a pipe,
 *   no data is copied through the kernel - both processes see the same bytes.
 *   But there is NO built-in synchronization, so we add semaphores.
 *
 * NAMED SEMAPHORES (sem_open):
 *   sem_data  starts at 0 : "a message is waiting in the shared slot"
 *   sem_ack   starts at 1 : "the slot is free / the message was read"
 *   Parent : sem_wait(ack)  -> write slot -> sem_post(data)
 *   Child  : sem_wait(data) -> read slot  -> sem_post(ack)
 *   This hand-shake means the slot is never overwritten before it is read.
 *
 * CLEANUP: the parent must munmap, close, sem_unlink and shm_unlink the
 * names it created.  If shm_unlink is forgotten the object stays in
 * /dev/shm even after the program exits.
 *
 * Names include the parent's process id (e.g. /cc_shm_1234) so we never
 * clash with, or delete, anybody else's objects.
 */
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "campuscompute.h"

#define MSG_DATA 0
#define MSG_END  1
#define WAIT_SECONDS 5          /* never wait forever: avoids a hang if the other side dies */

/* The layout of the shared memory block. */
struct Shared {
    int type;                   /* MSG_DATA or MSG_END                     */
    int job_id;
    int burst;
    int total_burst;            /* reply from the child                    */
    int job_count;              /* reply from the child                    */
};

/*
 * If the user presses Ctrl-C (SIGINT) or the program gets SIGTERM, the signal
 * handler only records the signal number.  The main code notices it, jumps to
 * its cleanup section (so the shared memory and semaphores are unlinked) and
 * only THEN lets the signal kill the program.  (SIGKILL cannot be caught, so
 * objects can still be left behind in that case: use "cleanup" to remove them.)
 */
static volatile sig_atomic_t got_signal;

static void on_signal(int sig)
{
    got_signal = sig;           /* the only thing a handler should do: set a flag */
}

/* sem_wait with a time limit.  Returns 0 on success, -1 on timeout/error/interrupt. */
static int wait_with_timeout(sem_t *sem)
{
    struct timespec limit;

    clock_gettime(CLOCK_REALTIME, &limit);
    limit.tv_sec += WAIT_SECONDS;
    while (sem_timedwait(sem, &limit) != 0) {
        if (errno == EINTR) {
            if (got_signal)
                return -1;      /* we were asked to stop */
            continue;           /* some other signal: try again */
        }
        return -1;              /* ETIMEDOUT or a real error */
    }
    return 0;
}

/* Code run by the CHILD process: it opens the objects BY NAME. */
static int child_main(const char *shm_name, const char *data_name, const char *ack_name)
{
    int fd;
    struct Shared *shared;
    sem_t *sem_data, *sem_ack;
    int total = 0, jobs = 0;

    /* the child inherited the parent's handlers; go back to the default behaviour */
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    fd = shm_open(shm_name, O_RDWR, 0600);
    if (fd < 0) {
        perror("child: shm_open");
        return 1;
    }
    shared = mmap(NULL, sizeof(struct Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);                                   /* the mapping stays valid */
    if (shared == MAP_FAILED) {
        perror("child: mmap");
        return 1;
    }
    sem_data = sem_open(data_name, 0);
    sem_ack = sem_open(ack_name, 0);
    if (sem_data == SEM_FAILED || sem_ack == SEM_FAILED) {
        perror("child: sem_open");
        return 1;
    }

    for (;;) {
        if (wait_with_timeout(sem_data) != 0) {   /* wait for a message */
            fprintf(stderr, "child: timed out waiting for the parent\n");
            return 1;
        }
        if (shared->type == MSG_END) {
            /* parent says "no more jobs": write the reply into shared memory */
            shared->total_burst = total;
            shared->job_count = jobs;
            printf("  [child  pid %d] END received -> reply: %d jobs, total burst %d\n",
                   (int)getpid(), jobs, total);
            sem_post(sem_ack);
            break;
        }
        total += shared->burst;
        jobs++;
        printf("  [child  pid %d] read  J%-2d burst %-3d (running total %d)\n",
               (int)getpid(), shared->job_id, shared->burst, total);
        sem_post(sem_ack);                        /* tell the parent the slot is free */
    }

    sem_close(sem_data);
    sem_close(sem_ack);
    munmap(shared, sizeof(struct Shared));
    return 0;
}

/* Print whether a named IPC object still exists (used to prove cleanup). */
static int shm_exists(const char *name)
{
    int fd = shm_open(name, O_RDONLY, 0);
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

static int sem_exists(const char *name)
{
    sem_t *s = sem_open(name, 0);
    if (s == SEM_FAILED)
        return 0;
    sem_close(s);
    return 1;
}

int cmd_ipc(int argc, char **argv)
{
    struct JobSet set;
    char shm_name[64], data_name[64], ack_name[64];
    int fd = -1;
    struct Shared *shared = MAP_FAILED;
    sem_t *sem_data = SEM_FAILED, *sem_ack = SEM_FAILED;
    pid_t child = -1;
    int expected = 0;
    int status = 0;
    int result = 1;                     /* assume failure until proven otherwise */
    int i;
    int shm_made = 0, data_made = 0, ack_made = 0;   /* which names did WE create? */
    struct sigaction action, old_int, old_term;

    /* ---- options ---- */
    if (argc == 3 && strcmp(argv[1], "-f") == 0) {
        if (jobs_load_file(&set, argv[2]) != 0)
            return 1;
    } else if (argc == 1) {
        jobs_load_default(&set);
    } else {
        fprintf(stderr, "usage: %s ipc [-f jobfile]\n", PROGRAM_NAME);
        return 2;
    }

    snprintf(shm_name, sizeof(shm_name), "/cc_shm_%d", (int)getpid());
    snprintf(data_name, sizeof(data_name), "/cc_sem_data_%d", (int)getpid());
    snprintf(ack_name, sizeof(ack_name), "/cc_sem_ack_%d", (int)getpid());

    /* catch Ctrl-C / SIGTERM so we can clean up before stopping (no SA_RESTART:
       we want sem_timedwait to be interrupted) */
    got_signal = 0;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, &old_int);
    sigaction(SIGTERM, &action, &old_term);

    printf("IPC demo: parent pid %d\n", (int)getpid());
    printf("  shared memory : %s\n  semaphores    : %s (initial 0), %s (initial 1)\n\n",
           shm_name, data_name, ack_name);

    /* ---- create the shared memory ---- */
    /* O_EXCL: fail if the name already exists, so we never reuse someone else's object */
    fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        perror("error: shm_open");
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
        return 1;
    }
    shm_made = 1;                       /* from now on WE own this name and must unlink it */
    if (ftruncate(fd, sizeof(struct Shared)) != 0) {      /* set its size */
        perror("error: ftruncate");
        goto cleanup;
    }
    shared = mmap(NULL, sizeof(struct Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("error: mmap");
        goto cleanup;
    }
    memset(shared, 0, sizeof(struct Shared));

    /* ---- create the named semaphores ---- */
    sem_data = sem_open(data_name, O_CREAT | O_EXCL, 0600, 0);
    if (sem_data == SEM_FAILED) {
        perror("error: sem_open (data)");
        goto cleanup;
    }
    data_made = 1;
    sem_ack = sem_open(ack_name, O_CREAT | O_EXCL, 0600, 1);
    if (sem_ack == SEM_FAILED) {
        perror("error: sem_open (ack)");
        goto cleanup;
    }
    ack_made = 1;

    /* ---- create the second process ---- */
    if (got_signal)
        goto cleanup;
    fflush(stdout);                     /* so buffered text is not printed twice */
    child = fork();
    if (child < 0) {
        perror("error: fork");
        goto cleanup;
    }
    if (child == 0)
        _exit(child_main(shm_name, data_name, ack_name));    /* CHILD never returns */

    /* ---- PARENT: send every job through the shared slot ---- */
    for (i = 0; i < set.count; i++) {
        if (wait_with_timeout(sem_ack) != 0) {
            if (!got_signal)
                fprintf(stderr, "parent: timed out waiting for the child\n");
            goto cleanup;
        }
        shared->type = MSG_DATA;
        shared->job_id = set.jobs[i].id;
        shared->burst = set.jobs[i].burst;
        expected += set.jobs[i].burst;
        printf("  [parent pid %d] wrote J%-2d burst %-3d\n",
               (int)getpid(), shared->job_id, shared->burst);
        sem_post(sem_data);
    }

    /* tell the child there are no more jobs, then wait for its reply */
    if (wait_with_timeout(sem_ack) != 0) {
        if (!got_signal)
            fprintf(stderr, "parent: timed out waiting for the child\n");
        goto cleanup;
    }
    shared->type = MSG_END;
    sem_post(sem_data);
    if (wait_with_timeout(sem_ack) != 0) {
        if (!got_signal)
            fprintf(stderr, "parent: timed out waiting for the child's reply\n");
        goto cleanup;
    }

    printf("\n  [parent] reply from child through shared memory: %d jobs, total burst %d\n",
           shared->job_count, shared->total_burst);
    printf("  [parent] parent's own sum of bursts: %d\n", expected);

    /* reap the child so it does not become a zombie */
    if (waitpid(child, &status, 0) < 0) {
        perror("error: waitpid");
        goto cleanup;
    }
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "error: child did not exit cleanly\n");
        goto cleanup;
    }

    if (shared->total_burst == expected && shared->job_count == set.count) {
        printf("  [parent] data exchanged correctly through shared memory: OK\n");
        result = 0;
    } else {
        printf("  [parent] MISMATCH between the two processes\n");
    }

cleanup:
    /* If we get here early with the child still alive, stop it and reap it. */
    if (child > 0) {
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
    }

    printf("\nCleanup:\n");
    if (got_signal)
        printf("  interrupted by signal %d - releasing IPC objects before stopping\n", (int)got_signal);
    if (sem_data != SEM_FAILED)
        sem_close(sem_data);
    if (sem_ack != SEM_FAILED)
        sem_close(sem_ack);
    if (shared != MAP_FAILED)
        munmap(shared, sizeof(struct Shared));
    if (fd >= 0)
        close(fd);

    /* sem_unlink / shm_unlink remove the NAMES - only the ones this run created */
    if (data_made && sem_unlink(data_name) == 0)
        printf("  sem_unlink  %s\n", data_name);
    if (ack_made && sem_unlink(ack_name) == 0)
        printf("  sem_unlink  %s\n", ack_name);
    if (shm_made && shm_unlink(shm_name) == 0)
        printf("  shm_unlink  %s\n", shm_name);

    /* prove it: opening the names again must now fail */
    printf("  re-open check: shared memory exists? %s, semaphores exist? %s\n",
           shm_exists(shm_name) ? "YES (leak!)" : "no",
           (sem_exists(data_name) || sem_exists(ack_name)) ? "YES (leak!)" : "no");
    if (shm_exists(shm_name) || sem_exists(data_name) || sem_exists(ack_name))
        result = 1;

    /* put the old signal handlers back; if we were interrupted, now let the signal take effect */
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    if (got_signal) {
        int sig = got_signal;
        signal(sig, SIG_DFL);
        raise(sig);
    }

    return result;
}
