/*
 * SPDX-License-Identifier: MIT
 *
 * Helpers shared by the P-a spike tools. See mta_util.h.
 */
#define _GNU_SOURCE
#include "mta_util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

long long mta_mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

double mta_mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int mta_results_dir(char *out, size_t n)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        snprintf(out, n, ".");
        return 0;
    }
    snprintf(out, n, "%s/mta-results", home);
    if (mkdir(out, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

void mta_hostname(char *out, size_t n)
{
    if (gethostname(out, n) != 0 || !out[0]) snprintf(out, n, "host");
    out[n - 1] = '\0';
    for (char *p = out; *p; ++p)
        if (*p == '.') { *p = '\0'; break; }
}

void mta_default_out(char *out, size_t n, const char *tool, const char *ext)
{
    char dir[PATH_MAX], host[64], stamp[32];
    if (mta_results_dir(dir, sizeof(dir)) != 0) snprintf(dir, sizeof(dir), ".");
    mta_hostname(host, sizeof(host));
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
    snprintf(out, n, "%s/%s-%s-%s.%s", dir, tool, host, stamp, ext);
}

static int read_small(const char *path, char *buf, size_t n)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, n - 1);
    close(fd);
    if (r < 0) return -1;
    buf[r] = '\0';
    return (int)r;
}

static int is_pid_name(const char *s)
{
    if (!*s) return 0;
    for (; *s; ++s)
        if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

int mta_find_launcher(pid_t *pids, int max)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        if (!is_pid_name(e->d_name)) continue;
        char path[64], comm[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", e->d_name);
        if (read_small(path, comm, sizeof(comm)) <= 0) continue;
        comm[strcspn(comm, "\n")] = '\0';
        if (strcmp(comm, "cyberdeckzero") == 0 || strcmp(comm, "APPLaunch") == 0)
            pids[count++] = (pid_t)atoi(e->d_name);
    }
    closedir(d);
    return count;
}

int mta_path_holders(const char *path, pid_t except_pgid, char *desc, size_t n)
{
    char want[PATH_MAX];
    if (!realpath(path, want)) snprintf(want, sizeof(want), "%s", path);
    desc[0] = '\0';
    DIR *d = opendir("/proc");
    if (!d) return 0;
    int count = 0;
    const pid_t self = getpid();
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!is_pid_name(e->d_name)) continue;
        const pid_t pid = (pid_t)atoi(e->d_name);
        if (pid == self) continue;
        if (except_pgid > 0 && mta_proc_pgid(pid, NULL) == except_pgid) continue;
        char fddir[64];
        snprintf(fddir, sizeof(fddir), "/proc/%d/fd", (int)pid);
        DIR *fds = opendir(fddir);
        if (!fds) continue;
        struct dirent *f;
        int hit = 0;
        while (!hit && (f = readdir(fds)) != NULL) {
            if (f->d_name[0] == '.') continue;
            char link[128], target[PATH_MAX];
            snprintf(link, sizeof(link), "%s/%s", fddir, f->d_name);
            ssize_t r = readlink(link, target, sizeof(target) - 1);
            if (r <= 0) continue;
            target[r] = '\0';
            if (strcmp(target, want) == 0) hit = 1;
        }
        closedir(fds);
        if (!hit) continue;
        char cpath[64], comm[64] = "?";
        snprintf(cpath, sizeof(cpath), "/proc/%d/comm", (int)pid);
        if (read_small(cpath, comm, sizeof(comm)) > 0) comm[strcspn(comm, "\n")] = '\0';
        size_t used = strlen(desc);
        if (used + 1 < n) snprintf(desc + used, n - used, "%s%d:%s", count ? "," : "", (int)pid, comm);
        ++count;
    }
    closedir(d);
    return count;
}

/* trims spaces and one pair of surrounding quotes in place */
static char *trim_value(char *v)
{
    while (*v == ' ' || *v == '\t') ++v;
    size_t len = strlen(v);
    while (len && (v[len - 1] == '\n' || v[len - 1] == '\r' || v[len - 1] == ' ' || v[len - 1] == '\t')) v[--len] = '\0';
    if (len >= 2 && ((v[0] == '"' && v[len - 1] == '"') || (v[0] == '\'' && v[len - 1] == '\''))) {
        v[len - 1] = '\0';
        ++v;
    }
    return v;
}

int mta_kv_file_get(const char *path, const char *key, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1024];
    const size_t klen = strlen(key);
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#') continue;
        if (strncmp(p, "export ", 7) == 0) p += 7;
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            snprintf(out, n, "%s", trim_value(p + klen + 1));
            found = 0; /* the last assignment wins, as in a shell */
        }
    }
    fclose(f);
    return found;
}

int mta_proc_env_get(pid_t pid, const char *key, char *out, size_t n)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/environ", (int)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    static char buf[65536];
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) return -1;
    buf[r] = '\0';
    const size_t klen = strlen(key);
    for (char *p = buf; p < buf + r; p += strlen(p) + 1) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            snprintf(out, n, "%s", p + klen + 1);
            return 0;
        }
    }
    return -1;
}

int mta_load_env_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[2048];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        if (strncmp(p, "export ", 7) == 0) p += 7;
        char *eq = strchr(p, '=');
        if (!eq || eq == p) continue;
        *eq = '\0';
        int ok = 1;
        for (char *k = p; *k; ++k)
            if (!(isalnum((unsigned char)*k) || *k == '_')) ok = 0;
        if (!ok) continue;
        if (setenv(p, trim_value(eq + 1), 1) == 0) ++count;
    }
    fclose(f);
    return count;
}

int mta_run(char *const argv[])
{
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
        if (errno != EINTR) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int mta_launcher_service(const char *verb)
{
    const char *sc = getenv("MTA_SYSTEMCTL");
    char *argv[] = {(char *)(sc && sc[0] ? sc : "systemctl"), (char *)"--user", (char *)verb,
                    (char *)"cyberdeckzero.service", NULL};
    return mta_run(argv);
}

pid_t mta_proc_pgid(pid_t pid, char *state)
{
    char path[64], buf[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    if (read_small(path, buf, sizeof(buf)) <= 0) return -1;
    /* pid (comm) state ppid pgrp ...: comm may hold spaces and ')' -> use the last ')' */
    char *rp = strrchr(buf, ')');
    if (!rp) return -1;
    char st = 0;
    int ppid = 0, pgrp = -1;
    if (sscanf(rp + 1, " %c %d %d", &st, &ppid, &pgrp) != 3) return -1;
    if (state) *state = st;
    return (pid_t)pgrp;
}

int mta_group_members(pid_t pgid, pid_t *pids, int max)
{
    DIR *d = opendir("/proc");
    if (!d) return 0;
    int count = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && count < max) {
        if (!is_pid_name(e->d_name)) continue;
        const pid_t pid = (pid_t)atoi(e->d_name);
        char st = 0;
        if (mta_proc_pgid(pid, &st) == pgid && st != 'Z' && st != 'X') pids[count++] = pid;
    }
    closedir(d);
    return count;
}

int mta_group_stopped(pid_t pgid)
{
    pid_t pids[256];
    const int n = mta_group_members(pgid, pids, 256);
    if (n == 0) return -1;
    for (int i = 0; i < n; ++i) {
        char st = 0;
        if (mta_proc_pgid(pids[i], &st) < 0) continue;
        if (st != 'T' && st != 't') return 0;
    }
    return 1;
}

void mta_sleep_ms(double ms, volatile sig_atomic_t *stop)
{
    if (ms <= 0) return;
    struct timespec ts = {(time_t)(ms / 1000.0), (long)((ms - (double)(long long)(ms / 1000.0) * 1000.0) * 1e6)};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        if (stop && *stop) return;
}
