/*
 * engine.c — OS-Jackfruit User-Space Container Runtime
 *
 * Phases implemented here (fill in each phase):
 *   Phase 1: Single container launch (namespaces + chroot)
 *   Phase 2: Supervisor daemon + UNIX socket CLI
 *   Phase 3: Bounded-buffer logging (pipe + ring buffer + threads)
 *   Phase 4: Kernel module integration (ioctl)
 *   Phase 5: Scheduler experiments (nice values)
 *
 * Build: make engine
 * Run:   sudo ./engine supervisor ./rootfs-base
 *        sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sched.h>
#include <pthread.h>

#include "monitor_ioctl.h"

/* ── Constants ────────────────────────────────────────────────────────────── */
#define MAX_CONTAINERS  16
#define SOCKET_PATH     "/tmp/engine.sock"
#define LOG_DIR         "/tmp"
#define RING_SIZE       (64 * 1024)   /* 64 KB ring buffer per container */

/* ── Container metadata ───────────────────────────────────────────────────── */
typedef struct {
    char            name[64];
    pid_t           host_pid;
    time_t          start_time;
    char            state[16];      /* starting | running | stopped | killed */
    int             soft_mib;
    int             hard_mib;
    char            log_path[256];
    int             exit_status;
    int             pipe_read_fd;
    pthread_mutex_t lock;
} Container;

/* ── Ring buffer ──────────────────────────────────────────────────────────── */
typedef struct {
    char            buf[RING_SIZE];
    int             head;
    int             tail;
    int             count;
    int             done;           /* set when container exits */
    pthread_mutex_t lock;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} RingBuffer;

/* ── Globals ──────────────────────────────────────────────────────────────── */
static Container            containers[MAX_CONTAINERS];
static pthread_mutex_t      containers_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t shutdown_flag  = 0;
static int                  monitor_fd      = -1;  /* /dev/container_monitor */

/* ── Forward declarations ─────────────────────────────────────────────────── */
static void usage(const char *prog);
static int  cmd_supervisor(const char *rootfs);
static int  cmd_start(int argc, char *argv[]);
static int  cmd_ps(void);
static int  cmd_stop(const char *name);
static int  cmd_logs(const char *name);

/* ── Signal handlers ──────────────────────────────────────────────────────── */
static void sigchld_handler(int sig)
{
    (void)sig;
    int   saved_errno = errno;
    int   status;
    pid_t pid;

    /* Reap all exited children — loop until no more */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        pthread_mutex_lock(&containers_lock);
        for (int i = 0; i < MAX_CONTAINERS; i++) {
            pthread_mutex_lock(&containers[i].lock);
            if (containers[i].host_pid == pid) {
                if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
                    strncpy(containers[i].state, "killed", sizeof(containers[i].state) - 1);
                else
                    strncpy(containers[i].state, "stopped", sizeof(containers[i].state) - 1);
                containers[i].exit_status = status;
            }
            pthread_mutex_unlock(&containers[i].lock);
        }
        pthread_mutex_unlock(&containers_lock);
    }
    errno = saved_errno;
}

static void sigterm_handler(int sig)
{
    (void)sig;
    shutdown_flag = 1;
}

/* ── Entry point ──────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* Initialise container array */
    for (int i = 0; i < MAX_CONTAINERS; i++) {
        memset(&containers[i], 0, sizeof(containers[i]));
        pthread_mutex_init(&containers[i].lock, NULL);
        strncpy(containers[i].state, "empty", sizeof(containers[i].state) - 1);
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "supervisor") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s supervisor <rootfs>\n", argv[0]); return 1; }
        return cmd_supervisor(argv[2]);
    } else if (strcmp(cmd, "start") == 0) {
        return cmd_start(argc - 2, argv + 2);
    } else if (strcmp(cmd, "ps") == 0) {
        return cmd_ps();
    } else if (strcmp(cmd, "stop") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s stop <name>\n", argv[0]); return 1; }
        return cmd_stop(argv[2]);
    } else if (strcmp(cmd, "logs") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s logs <name>\n", argv[0]); return 1; }
        return cmd_logs(argv[2]);
    } else {
        usage(argv[0]);
        return 1;
    }
}

/* ── Usage ────────────────────────────────────────────────────────────────── */
static void usage(const char *prog)
{
    fprintf(stderr,
        "OS-Jackfruit Container Runtime\n\n"
        "Usage:\n"
        "  %s supervisor <rootfs>                          Start supervisor daemon\n"
        "  %s start <name> <rootfs> <cmd> [--soft-mib N] [--hard-mib N]\n"
        "  %s ps                                           List containers\n"
        "  %s stop <name>                                  Stop a container\n"
        "  %s logs <name>                                  Print container logs\n",
        prog, prog, prog, prog, prog);
}

/* ── Phase 1: Launch a single container ──────────────────────────────────── */
/*
 * TODO (Phase 1): Implement container_launch()
 *
 * Steps:
 *   1. pipe(fds) — capture stdout/stderr
 *   2. fork()
 *   3. Child:
 *      a. unshare(CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS)
 *      b. dup2(fds[1], STDOUT_FILENO); dup2(fds[1], STDERR_FILENO)
 *      c. close(fds[0]); close(fds[1])
 *      d. chroot(rootfs_path)
 *      e. chdir("/")
 *      f. mount("proc", "/proc", "proc", 0, NULL)
 *      g. execv(cmd, args)
 *   4. Parent:
 *      a. close(fds[1])
 *      b. Store fds[0] in container metadata as pipe_read_fd
 *      c. Register PID with kernel module (Phase 4)
 *      d. Spawn producer/consumer logging threads (Phase 3)
 */
static pid_t container_launch(const char *name, const char *rootfs,
                               char *const cmd_args[], int soft_mib, int hard_mib)
{
    (void)name; (void)rootfs; (void)cmd_args; (void)soft_mib; (void)hard_mib;
    fprintf(stderr, "[TODO] container_launch() not yet implemented (Phase 1)\n");
    return -1;
}

/* ── Phase 2: Supervisor daemon ──────────────────────────────────────────── */
static int cmd_supervisor(const char *rootfs)
{
    (void)rootfs;

    /* Install signal handlers */
    struct sigaction sa_chld = { .sa_handler = sigchld_handler, .sa_flags = SA_RESTART };
    struct sigaction sa_term = { .sa_handler = sigterm_handler,  .sa_flags = 0 };
    sigemptyset(&sa_chld.sa_mask);
    sigemptyset(&sa_term.sa_mask);
    sigaction(SIGCHLD, &sa_chld, NULL);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT,  &sa_term, NULL);

    /*
     * TODO (Phase 2): Implement UNIX domain socket supervisor loop
     *
     * Steps:
     *   1. socket(AF_UNIX, SOCK_STREAM, 0)
     *   2. bind() to SOCKET_PATH
     *   3. listen()
     *   4. Loop: accept() -> read command -> dispatch -> write response
     *   5. Check shutdown_flag each iteration
     *   6. On shutdown: SIGTERM all containers, wait, cleanup
     */
    fprintf(stderr, "[TODO] Supervisor not yet implemented (Phase 2)\n");
    fprintf(stderr, "       Socket path: %s\n", SOCKET_PATH);
    return 1;
}

/* ── Phase 2: CLI commands (connect to supervisor) ───────────────────────── */
static int cmd_start(int argc, char *argv[])
{
    (void)argc; (void)argv;
    /*
     * TODO (Phase 2): Connect to SOCKET_PATH, send START command, read response
     */
    fprintf(stderr, "[TODO] CLI start not yet implemented (Phase 2)\n");
    return 1;
}

static int cmd_ps(void)
{
    /*
     * TODO (Phase 2): Connect to SOCKET_PATH, send PS command, print response
     */
    fprintf(stderr, "[TODO] CLI ps not yet implemented (Phase 2)\n");
    return 1;
}

static int cmd_stop(const char *name)
{
    (void)name;
    /*
     * TODO (Phase 2): Connect to SOCKET_PATH, send STOP <name>, read response
     */
    fprintf(stderr, "[TODO] CLI stop not yet implemented (Phase 2)\n");
    return 1;
}

static int cmd_logs(const char *name)
{
    (void)name;
    /*
     * TODO (Phase 2): Connect to SOCKET_PATH, send LOGS <name>, print response
     */
    fprintf(stderr, "[TODO] CLI logs not yet implemented (Phase 2)\n");
    return 1;
}
