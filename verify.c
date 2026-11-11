/*
 * verify.c - FR7: cleanup verification and cleanup of leftovers
 *
 * verify  : read-only checks that nothing created by this program is left
 *           - no child process (or zombie) belonging to us
 *           - only one thread in this process (all others were joined)
 *           - no shared memory / semaphore objects with our name prefix
 * cleanup : removes leftover IPC objects whose names start with our prefix
 *           (cc_shm_, cc_sem_) - ONLY those, never anybody else's objects.
 *           With --logs it also deletes the log file this program writes.
 *
 * On Linux, POSIX shared memory lives as files in /dev/shm, and a named
 * semaphore "/name" appears there as "sem.name".
 */
#include <dirent.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "campuscompute.h"

#define SHM_DIR   "/dev/shm"
#define LOG_FILE  "campuscompute.log"

/* Does this file name in /dev/shm belong to us?  kind: 1 = shm, 2 = semaphore, 0 = not ours */
static int our_object(const char *name)
{
    if (strncmp(name, "cc_shm_", 7) == 0)
        return 1;
    if (strncmp(name, "sem.cc_sem_", 11) == 0)
        return 2;
    return 0;
}

/*
 * Our object names end with the process id of the run that created them
 * (e.g. cc_shm_1234).  Return 1 if that process is still running, so that
 * "cleanup" never pulls the objects out from under a program that is using them.
 */
static int owner_is_running(const char *name)
{
    const char *last = strrchr(name, '_');
    long pid;
    char *end;

    if (last == NULL)
        return 0;
    pid = strtol(last + 1, &end, 10);
    if (end == last + 1 || *end != '\0' || pid <= 0)
        return 0;
    /* kill() with signal 0 only checks that the process exists */
    return kill((pid_t)pid, 0) == 0 || errno == EPERM;
}

/* Count entries in a directory, ignoring "." and "..". */
static int count_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *e;
    int n = 0;

    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0)
            n++;
    closedir(d);
    return n;
}

int cmd_verify(int argc, char **argv)
{
    DIR *d;
    struct dirent *e;
    int problems = 0;
    int leftovers = 0;
    int threads;
    pid_t w;

    (void)argv;
    if (argc != 1) {
        fprintf(stderr, "usage: %s verify\n", PROGRAM_NAME);
        return 2;
    }

    printf("Cleanup verification\n");

    /* 1. child processes: waitpid with WNOHANG returns -1/ECHILD when there are none */
    errno = 0;
    w = waitpid(-1, NULL, WNOHANG);
    if (w == -1 && errno == ECHILD) {
        printf("  [OK]   child processes : none left (waitpid -> ECHILD), no zombies\n");
    } else {
        printf("  [FAIL] child processes : a child process still exists\n");
        problems++;
    }

    /* 2. threads: /proc/self/task has one entry per thread */
    threads = count_entries("/proc/self/task");
    if (threads == 1) {
        printf("  [OK]   threads         : 1 (only the main thread; all worker threads were joined)\n");
    } else if (threads < 0) {
        printf("  [SKIP] threads         : /proc is not available here\n");
    } else {
        printf("  [FAIL] threads         : %d threads still running\n", threads);
        problems++;
    }

    /* 3. shared memory and semaphores with our prefixes */
    d = opendir(SHM_DIR);
    if (d == NULL) {
        printf("  [SKIP] IPC objects     : cannot read %s (%s)\n", SHM_DIR, strerror(errno));
    } else {
        while ((e = readdir(d)) != NULL) {
            if (our_object(e->d_name)) {
                printf("  [FAIL] IPC objects     : leftover %s/%s\n", SHM_DIR, e->d_name);
                leftovers++;
            }
        }
        closedir(d);
        if (leftovers == 0)
            printf("  [OK]   IPC objects     : no cc_shm_* or cc_sem_* objects in %s\n", SHM_DIR);
        else
            problems++;
    }

    /* 4. the log file is an OUTPUT of the program, so just report it */
    if (access(LOG_FILE, F_OK) == 0)
        printf("  [INFO] log file        : %s exists (program output, remove with 'cleanup --logs')\n", LOG_FILE);
    else
        printf("  [INFO] log file        : %s does not exist\n", LOG_FILE);

    printf("Verification result: %s\n", problems == 0 ? "CLEAN - nothing left behind"
                                                      : "PROBLEMS FOUND (run 'cleanup')");
    return problems == 0 ? 0 : 1;
}

int cmd_cleanup(int argc, char **argv)
{
    int remove_logs = 0;
    int removed = 0;
    DIR *d;
    struct dirent *e;

    if (argc == 2 && strcmp(argv[1], "--logs") == 0) {
        remove_logs = 1;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s cleanup [--logs]\n", PROGRAM_NAME);
        return 2;
    }

    d = opendir(SHM_DIR);
    if (d == NULL) {
        printf("Cannot read %s (%s): nothing to clean.\n", SHM_DIR, strerror(errno));
    } else {
        while ((e = readdir(d)) != NULL) {
            int kind = our_object(e->d_name);
            char name[300];             /* d_name can be up to 255 characters */

            if (kind == 0)
                continue;               /* not ours: never touch it */
            if (owner_is_running(e->d_name)) {
                printf("skipped %s (the process that created it is still running)\n", e->d_name);
                continue;
            }
            if (kind == 1) {
                snprintf(name, sizeof(name), "/%s", e->d_name);
                if (shm_unlink(name) == 0) {
                    printf("removed shared memory %s\n", name);
                    removed++;
                }
            } else {
                snprintf(name, sizeof(name), "/%s", e->d_name + 4);   /* drop the "sem." prefix */
                if (sem_unlink(name) == 0) {
                    printf("removed semaphore %s\n", name);
                    removed++;
                }
            }
        }
        closedir(d);
    }

    if (remove_logs) {
        if (unlink(LOG_FILE) == 0) {
            printf("removed %s\n", LOG_FILE);
            removed++;
        } else if (errno != ENOENT) {
            perror("cannot remove log file");
        }
    }

    printf("Cleanup finished: %d object(s) removed.\n", removed);
    return 0;
}
