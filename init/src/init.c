/*
 * claw-init - ClawOS Init System
 *
 * Minimal PID 1 init that boots the system fast and launches
 * clawd as the first service. Designed to replace systemd/openrc
 * with an agent-native boot process.
 *
 * Boot sequence:
 *   1. Mount essential filesystems
 *   2. Set hostname
 *   3. Start clawd (kernel daemon)
 *   4. Start claw-bus (message bus)
 *   5. Start OpenClaw runtime
 *   6. Run user-defined init scripts from /etc/claw/init.d/
 *   7. Wait and reap zombies
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/reboot.h>

#define CLAWD_PATH       "/usr/sbin/clawd"
#define CLAW_BUS_PATH    "/usr/sbin/claw-bus"
#define OPENCLAW_PATH    "/usr/sbin/openclaw-runtime"
#define INIT_SCRIPTS_DIR "/etc/claw/init.d"
#define HOSTNAME_FILE    "/etc/hostname"

static volatile int running = 1;
static volatile int reboot_flag = 0;

static void sig_handler(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        running = 0;
        break;
    case SIGUSR1:
        running = 0;
        reboot_flag = 1;
        break;
    }
}

static void log_msg(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "[claw-init] ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static int mount_fs(const char *src, const char *dst, const char *type,
                    unsigned long flags)
{
    mkdir(dst, 0755);
    if (mount(src, dst, type, flags, NULL) < 0) {
        if (errno != EBUSY) {  /* already mounted is fine */
            log_msg("mount %s on %s failed: %s", type, dst, strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void mount_essential(void)
{
    mount_fs("proc",    "/proc",    "proc",    MS_NOSUID | MS_NODEV | MS_NOEXEC);
    mount_fs("sysfs",   "/sys",     "sysfs",   MS_NOSUID | MS_NODEV | MS_NOEXEC);
    mount_fs("devtmpfs", "/dev",    "devtmpfs", MS_NOSUID);
    mount_fs("tmpfs",   "/tmp",     "tmpfs",   MS_NOSUID | MS_NODEV);
    mount_fs("tmpfs",   "/run",     "tmpfs",   MS_NOSUID | MS_NODEV);

    /* Create essential directories */
    mkdir("/run/claw", 0755);
    mkdir("/run/claw/ipc", 0755);
    mkdir("/var/log/claw", 0755);

    /* /dev/pts for terminals */
    mkdir("/dev/pts", 0755);
    mount_fs("devpts", "/dev/pts", "devpts", MS_NOSUID | MS_NOEXEC);

    /* cgroup v2 */
    mkdir("/sys/fs/cgroup", 0755);
    mount_fs("cgroup2", "/sys/fs/cgroup", "cgroup2", 0);

    /* ClawOS cgroup */
    mkdir("/sys/fs/cgroup/claw", 0755);

    log_msg("filesystems mounted");
}

static void set_hostname(void)
{
    char hostname[64] = "clawos";
    FILE *f = fopen(HOSTNAME_FILE, "r");
    if (f) {
        if (fgets(hostname, sizeof(hostname), f)) {
            /* Strip newline */
            char *nl = strchr(hostname, '\n');
            if (nl) *nl = '\0';
        }
        fclose(f);
    }
    sethostname(hostname, strlen(hostname));
    log_msg("hostname: %s", hostname);
}

static pid_t spawn(const char *path, const char *name)
{
    if (access(path, X_OK) != 0) {
        log_msg("skipping %s (not found)", name);
        return 0;
    }

    pid_t pid = fork();
    if (pid < 0) {
        log_msg("fork failed for %s: %s", name, strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execl(path, name, "-f", NULL);  /* -f = foreground */
        _exit(127);
    }

    log_msg("started %s (pid %d)", name, pid);
    return pid;
}

static void run_init_scripts(void)
{
    DIR *d = opendir(INIT_SCRIPTS_DIR);
    if (!d)
        return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char path[256];
        snprintf(path, sizeof(path), "%s/%s", INIT_SCRIPTS_DIR, ent->d_name);

        if (access(path, X_OK) == 0) {
            log_msg("running init script: %s", ent->d_name);
            pid_t pid = fork();
            if (pid == 0) {
                execl("/bin/sh", "sh", path, NULL);
                _exit(127);
            }
            if (pid > 0)
                waitpid(pid, NULL, 0);
        }
    }
    closedir(d);
}

static void shutdown_system(void)
{
    log_msg("system shutdown...");

    /* Send SIGTERM to all processes */
    kill(-1, SIGTERM);
    sleep(2);

    /* Force kill any remaining */
    kill(-1, SIGKILL);
    sleep(1);

    /* Unmount filesystems */
    umount2("/sys/fs/cgroup/claw", MNT_DETACH);
    umount2("/sys/fs/cgroup", MNT_DETACH);
    umount2("/dev/pts", MNT_DETACH);
    umount2("/run", MNT_DETACH);
    umount2("/tmp", MNT_DETACH);
    umount2("/dev", MNT_DETACH);
    umount2("/sys", MNT_DETACH);
    umount2("/proc", MNT_DETACH);

    sync();
}

int main(void)
{
    pid_t clawd_pid, bus_pid, openclaw_pid;

    if (getpid() != 1) {
        fprintf(stderr, "claw-init: must be run as PID 1\n");
        return 1;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);
    signal(SIGUSR1, sig_handler);  /* SIGUSR1 = reboot */
    signal(SIGCHLD, SIG_DFL);

    log_msg("ClawOS booting...");

    /* Phase 1: Mount essential filesystems */
    mount_essential();

    /* Phase 2: Set hostname */
    set_hostname();

    /* Phase 3: Start core services */
    clawd_pid = spawn(CLAWD_PATH, "clawd");
    usleep(200000);  /* give clawd time to create socket */

    bus_pid = spawn(CLAW_BUS_PATH, "claw-bus");
    usleep(100000);

    openclaw_pid = spawn(OPENCLAW_PATH, "openclaw-runtime");
    usleep(100000);

    /* Phase 4: Run user init scripts */
    run_init_scripts();

    log_msg("system ready (clawd=%d bus=%d openclaw=%d)",
            clawd_pid, bus_pid, openclaw_pid);

    /* Phase 5: Main loop - reap zombies */
    while (running) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD) {
                sleep(1);
                continue;
            }
        }

        if (pid > 0) {
            /* Restart core services if they die */
            if (pid == clawd_pid && running) {
                log_msg("clawd died, restarting...");
                usleep(500000);
                clawd_pid = spawn(CLAWD_PATH, "clawd");
            } else if (pid == bus_pid && running) {
                log_msg("claw-bus died, restarting...");
                usleep(500000);
                bus_pid = spawn(CLAW_BUS_PATH, "claw-bus");
            } else if (pid == openclaw_pid && running) {
                log_msg("openclaw-runtime died, restarting...");
                usleep(500000);
                openclaw_pid = spawn(OPENCLAW_PATH, "openclaw-runtime");
            }
        }
    }

    shutdown_system();

    if (reboot_flag)
        reboot(RB_AUTOBOOT);
    else
        reboot(RB_POWER_OFF);

    return 0;
}
