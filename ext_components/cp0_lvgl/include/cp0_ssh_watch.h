/*
 * SPDX-License-Identifier: MIT
 *
 * Incoming SSH sessions for the status strip's SSH indicator (study "SSH session indicator in the status strip",
 * 2026-10-30): how many incoming SSH connections exist and whether bytes moved on them since the previous sample.
 * Plain C, no LVGL, no thread, no fork, no logging (peer addresses never leave this module except through
 * cp0_ssh_watch_peers()).
 *
 * DETECTION. /proc/net/tcp and /proc/net/tcp6, state ESTABLISHED (01), local port in sshd's port set:
 *   - the config ports: every `Port` line and every `ListenAddress host:port` / `[v6]:port` port of
 *     /etc/ssh/sshd_config and the files of its `Include` lines (glob patterns, relative ones under /etc/ssh, nested up
 *     to 4 deep; keywords case-insensitive, `Keyword value` or `Keyword=value`, `#` starts a comment). No `Port` line:
 *     22 (sshd's default). An unreadable or missing sshd_config: the files matching /etc/ssh/sshd_config.d/<*>.conf
 *     (the Debian default Include), else 22. Re-read every CP0_SSH_CONFIG_PERIOD_MS, or at the next sample after cp0_ssh_watch_reload();
 *   - $APPLAUNCH_SSH_PORTS ("22,2222", commas or spaces; a board.conf key) replaces the config ports when it holds at
 *     least one valid port (1..65535): the escape hatch for a hand-edited ssh.socket. Read with the config;
 *   - a port counts only while a LISTEN (0A) socket exists on it in either file, so nothing shows while sshd is off.
 * The server side of a connection has the sshd port as its LOCAL port; the launcher's own outgoing `ssh` has an
 * ephemeral local port and remote port 22, so it never counts. `ssh localhost` counts once (its server side). IPv4
 * clients of a dual-stack listener (ssh.socket's [::]:22) appear in tcp6 as v4-mapped addresses and count like any
 * other; cp0_ssh_watch_peers() prints them as IPv4. Connections in any other state (SYN_RECV, TIME_WAIT, CLOSE_WAIT,
 * ...) never count. Pre-authentication connections (a password prompt, a bot) count: they are TCP connections to sshd.
 *
 * CONFIRMATION (port scans and handshake-only attempts never show). A connection is identified by its address family,
 * local address and port, remote address and port. It counts from the first sample at which (a) it was also present in
 * the previous sample, and in every sample since it was first seen, and (b) it was first seen at least
 * CP0_SSH_CONFIRM_MS earlier. At the designed call rate (every 2 s) that is exactly "seen in two consecutive samples";
 * (b) keeps a faster caller from shortening the debounce. A connection missing from one sample starts over.
 *
 * ACTIVITY. `active` = 1 when, on at least one counted connection, at least CP0_SSH_ACTIVE_MIN_BYTES bytes moved since
 * the previous sample (tcp_info's tcpi_bytes_acked + tcpi_bytes_received, both directions), read with one direct
 * NETLINK_SOCK_DIAG (inet_diag) dump per address family that has a candidate connection: no fork, no root, a
 * non-blocking socket with a CP0_SSH_DIAG_TIMEOUT_MS deadline. No candidate connection: no dump at all. A connection's
 * first sighting has no baseline and is never activity; its second (when it starts to count) compares with the first.
 * The minimum is 1 byte, i.e. no keepalive filter: an SSH-level keepalive request or reply (ClientAliveInterval /
 * ServerAliveInterval) is one encrypted packet of a few tens of bytes, the same size as one keystroke and its echo, so
 * any threshold that hides keepalives also hides slow typing. Such keepalives flash once per interval (accepted by the
 * owner); TCP keepalives (sshd's default TCPKeepAlive) carry no payload bytes and never count.
 * FALLBACK (source CP0_SSH_ACT_QUEUE, weak): when netlink is unavailable or refused (no tcp_diag, an error reply, the
 * deadline, a kernel tcp_info without byte counters), `active` = 1 when a counted connection has a non-zero
 * tx_queue or rx_queue column in /proc: that catches bulk transfers (scp, a flood of output), not typing. After a
 * netlink failure the module uses the fallback for CP0_SSH_DIAG_RETRY_MS, then tries netlink again.
 *
 * PATHS. Every path is <root> + the absolute path (/proc/net/tcp, /proc/net/tcp6, /etc/ssh/sshd_config, the Include
 * patterns). <root> is the option's `root`, else $APPLAUNCH_STATUS_SYSFS_ROOT when set and not empty (the status
 * strip's existing test hook: the render harness's fake tree), else none (the real files). Under a root the default
 * activity source is the queue fallback (a fake tree has no kernel sockets to ask netlink about).
 *
 * THREADING AND COST. A cp0_ssh_watch_t is plain memory owned by one caller: not thread-safe; call every function on
 * the same thread or under the caller's own mutex. It holds no file descriptor between calls (the netlink socket lives
 * for one sample), so it needs no teardown. Designed to be sampled every 2 s from the launcher's status feed thread
 * (calls at least about 1 s apart; see CONFIRMATION). One sample: two /proc reads through a fixed stack buffer (no
 * stdio, no heap), plus, only while a candidate connection exists, one netlink dump per family; the config re-read
 * (no stdio, but glob() allocates) happens at most every CP0_SSH_CONFIG_PERIOD_MS. Worst case blocking:
 * CP0_SSH_DIAG_TIMEOUT_MS per family when the kernel does not answer. Most of a sample's cost is in the kernel: each
 * /proc read and each dump walks the whole TCP established hash table, whose size follows the RAM (about 4096 buckets
 * on a 512 MB Pi Zero 2 W, 65536 on an 8 GB machine). Measured 2026-10-30 on an i5-12450H under WSL2 (65536 buckets,
 * about 95 us per walk): 215 us per sample without a session, 300 us with one; the config re-read 5 us.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_SSH_PORTS_ENV "APPLAUNCH_SSH_PORTS"
#define CP0_SSH_ROOT_ENV "APPLAUNCH_STATUS_SYSFS_ROOT"

#define CP0_SSH_MAX_PORTS 8          /* config ports kept (more are ignored) */
#define CP0_SSH_MAX_CONNS 16         /* connections tracked; more are not seen (count saturates here) */
#define CP0_SSH_CONFIRM_MS 1000      /* see CONFIRMATION */
#define CP0_SSH_ACTIVE_MIN_BYTES 1   /* see ACTIVITY */
#define CP0_SSH_CONFIG_PERIOD_MS 30000
#define CP0_SSH_DIAG_TIMEOUT_MS 50
#define CP0_SSH_DIAG_RETRY_MS 60000

#define CP0_SSH_TCP_ESTABLISHED 0x01 /* the kernel's TCP states as /proc prints them (st column) */
#define CP0_SSH_TCP_LISTEN 0x0A

/* How this sample measured activity. */
typedef enum {
    CP0_SSH_ACT_NONE = 0, /* no candidate connection: nothing to measure */
    CP0_SSH_ACT_BYTES,    /* netlink byte counters: exact */
    CP0_SSH_ACT_QUEUE,    /* the /proc queue columns: weak, bulk transfers only */
} cp0_ssh_act_source_t;

/* What cp0_ssh_watch_sample() returns. */
typedef struct {
    int count;                   /* confirmed incoming SSH connections (0..CP0_SSH_MAX_CONNS); 0 = hide the indicator */
    int active;                  /* 1: bytes moved on a confirmed connection since the previous sample; always 0 when
                                    count is 0 */
    cp0_ssh_act_source_t source; /* how `active` was measured */
    int listening;               /* sshd ports with a LISTEN socket (0: sshd is not listening, count is 0) */
} cp0_ssh_status_t;

/* One socket line of /proc/net/tcp or /proc/net/tcp6. Addresses are the kernel's in-memory words (network byte order,
 * as the file prints them: each 8-digit word is a native u32); an IPv4 address is word 0 only. Ports in host order. */
typedef struct {
    uint32_t local[4];
    uint32_t remote[4];
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t family;   /* 4 (tcp: 8 hex digits per address) or 6 (tcp6: 32 hex digits) */
    uint8_t state;    /* CP0_SSH_TCP_ESTABLISHED, CP0_SSH_TCP_LISTEN, ... */
    uint32_t tx_queue;
    uint32_t rx_queue;
    uint32_t uid;
    unsigned long inode;
} cp0_ssh_sock_t;

/* A tracked candidate connection (private to the module except for the counters hook below). */
typedef struct {
    cp0_ssh_sock_t sock;
    int64_t first_seen_ms;
    uint64_t bytes;    /* tcpi_bytes_acked + tcpi_bytes_received at this sample, when has_bytes */
    uint8_t has_bytes;
    uint8_t counted;   /* confirmed (see CONFIRMATION) */
} cp0_ssh_conn_t;

/* Fills conns[i].bytes and sets conns[i].has_bytes = 1 for each connection it finds (has_bytes is 0 on entry); a
 * connection it does not find keeps has_bytes 0. Returns 0 on success, -1 when the counters cannot be read at all (the
 * sample then uses the queue fallback). cp0_ssh_diag_counters() is the netlink implementation. */
typedef int (*cp0_ssh_counters_fn)(void *ctx, cp0_ssh_conn_t *conns, int count);

typedef struct {
    const char *root;              /* NULL: $APPLAUNCH_STATUS_SYSFS_ROOT when set and not empty, else none; "": none.
                                      At most 223 characters (copied) */
    cp0_ssh_counters_fn counters;  /* NULL: netlink without a root, the queue fallback under a root. Test hook. */
    void *counters_ctx;
} cp0_ssh_watch_options_t;

/* The watch's state. Fields are private: use the functions below. Zeroed or not, call cp0_ssh_watch_init() first. */
typedef struct {
    char root[224];
    cp0_ssh_counters_fn counters;
    void *counters_ctx;
    int use_netlink;
    uint16_t ports[CP0_SSH_MAX_PORTS];
    int nports;
    int ports_loaded;
    int64_t ports_read_ms;
    int diag_failed;
    int64_t diag_retry_ms;
    cp0_ssh_conn_t conns[CP0_SSH_MAX_CONNS];
    int nconns;
} cp0_ssh_watch_t;

/* A confirmed connection's peer, for the Settings info row (never for status.state, never for a log). */
typedef struct {
    char addr[46];      /* "192.168.68.12", "2001:db8::5"; a v4-mapped peer in its IPv4 form */
    uint16_t port;      /* the peer's (client's) port */
} cp0_ssh_peer_t;

/* Prepares *w (no I/O). options NULL = the defaults above. */
void cp0_ssh_watch_init(cp0_ssh_watch_t *w, const cp0_ssh_watch_options_t *options);

/* One sample at now_ms, a monotonic clock in milliseconds (cp0_ssh_monotonic_ms(), or a test's fake clock); used for
 * the confirmation delay, the config re-read period and the netlink retry. Fills *out (cleared first) and returns
 * out->count. Missing /proc files count as empty (no connection). w or out NULL: returns 0. */
int cp0_ssh_watch_sample(cp0_ssh_watch_t *w, int64_t now_ms, cp0_ssh_status_t *out);

/* Re-read the sshd config and $APPLAUNCH_SSH_PORTS at the next sample instead of waiting for the period. */
void cp0_ssh_watch_reload(cp0_ssh_watch_t *w);

/* The peers of the connections confirmed at the last sample, in /proc order: fills up to max entries and returns how
 * many it filled. */
int cp0_ssh_watch_peers(const cp0_ssh_watch_t *w, cp0_ssh_peer_t *peers, int max);

/* CLOCK_MONOTONIC in milliseconds. */
int64_t cp0_ssh_monotonic_ms(void);

/* ---- building blocks (used by the watch; public for tests and tools) ---- */

/* Parses one line of /proc/net/tcp or tcp6. Returns 1 and fills *out for a socket line, 0 for the header, an empty,
 * truncated or malformed line. */
int cp0_ssh_parse_tcp_line(const char *line, cp0_ssh_sock_t *out);

/* The config ports under root (NULL or "": the real /etc), $APPLAUNCH_SSH_PORTS first (see DETECTION), without the
 * LISTEN filter. Fills up to max unique ports in the order found and returns how many (at least 1 when max > 0). */
int cp0_ssh_config_ports(const char *root, uint16_t *ports, int max);

/* The netlink counters (a cp0_ssh_counters_fn; ctx unused). Matches by family, addresses and ports. Returns -1 and sets
 * errno when the dump fails (EPROTONOSUPPORT / ENOENT: no sock_diag; EPERM / EACCES: refused; ETIMEDOUT; ENOTSUP: the
 * kernel's tcp_info has no byte counters; EMSGSIZE: a truncated reply). */
int cp0_ssh_diag_counters(void *ctx, cp0_ssh_conn_t *conns, int count);

#ifdef __cplusplus
}
#endif
