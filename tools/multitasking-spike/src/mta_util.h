/*
 * SPDX-License-Identifier: MIT
 *
 * Small helpers shared by the P-a spike tools (study 026): clocks, the results folder, the launcher and framebuffer
 * owner checks, KEY=VALUE files, running a command. C11 + POSIX + Linux; no library.
 */
#pragma once

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

/* CLOCK_MONOTONIC in microseconds / milliseconds (the clock evdev uses after EVIOCSCLOCKID(CLOCK_MONOTONIC)). */
long long mta_mono_us(void);
double mta_mono_ms(void);

/* ~/mta-results (created when missing); "." when HOME is unset. 0 on success. */
int mta_results_dir(char *out, size_t n);

/* <results dir>/<tool>-<host>-<YYYYmmdd-HHMMSS>.<ext> */
void mta_default_out(char *out, size_t n, const char *tool, const char *ext);

/* hostname, never empty */
void mta_hostname(char *out, size_t n);

/* PIDs of running launcher processes (comm "cyberdeckzero" or "APPLaunch"), at most max; returns the count. */
int mta_find_launcher(pid_t *pids, int max);

/* Processes (other than self and except_pgid's group) with an open fd on path, readable in /proc (same user or
 * root). Writes "pid:comm,pid:comm" into desc; returns the count. */
int mta_path_holders(const char *path, pid_t except_pgid, char *desc, size_t n);

/* Value of KEY in a KEY=VALUE file ("#" comments, optional quotes). 0 when found. */
int mta_kv_file_get(const char *path, const char *key, char *out, size_t n);

/* Value of KEY in /proc/<pid>/environ. 0 when found. */
int mta_proc_env_get(pid_t pid, const char *key, char *out, size_t n);

/* setenv() every KEY=VALUE line of a file; returns the number set, -1 when the file cannot be read. */
int mta_load_env_file(const char *path);

/* fork + execvp + wait; stdout/stderr inherited. Returns the exit status (0..255), or -1. */
int mta_run(char *const argv[]);

/* systemctl --user <verb> cyberdeckzero.service ($MTA_SYSTEMCTL replaces systemctl: a test hook). */
int mta_launcher_service(const char *verb);

/* Process group of a pid from /proc/<pid>/stat, -1 when gone. State letter into *state when not NULL. */
pid_t mta_proc_pgid(pid_t pid, char *state);

/* PIDs whose process group is pgid (from /proc); returns the count (at most max). */
int mta_group_members(pid_t pgid, pid_t *pids, int max);

/* 1 when every member of the group is stopped ('T' or 't'), 0 when one is not, -1 when the group is empty. */
int mta_group_stopped(pid_t pgid);

/* Sleep ms milliseconds (nanosleep, restarts after signals unless *stop becomes non-zero). */
void mta_sleep_ms(double ms, volatile sig_atomic_t *stop);
