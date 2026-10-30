/*
 * SPDX-License-Identifier: MIT
 *
 * Incoming SSH sessions for the status strip's SSH indicator (see cp0_ssh_watch.h).
 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE /* glob, strncasecmp, O_CLOEXEC, clock_gettime */
#endif
#include "cp0_ssh_watch.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/tcp.h>
#include <netinet/in.h>
#include <poll.h>
#endif

#define INCLUDE_DEPTH_MAX 4

/* ----------------------------------------------------------------------------------------------------- line reader */

/* Lines of a file through one fixed buffer: no stdio, no heap. A line longer than the buffer is dropped whole; "\r\n"
 * ends a line too (a fake tree made on Windows). */
#define LR_SIZE 4096
typedef struct {
    int fd;
    int eof;
    int skipping; /* inside an over-long line: dropped up to its end */
    size_t pos, len;
    char buf[LR_SIZE + 1];
} line_reader_t;

static int lr_open(line_reader_t *r, const char *path)
{
    r->fd = open(path, O_RDONLY | O_CLOEXEC);
    r->eof = 0;
    r->skipping = 0;
    r->pos = r->len = 0;
    return r->fd >= 0;
}

static void lr_close(line_reader_t *r)
{
    if (r->fd >= 0) close(r->fd);
    r->fd = -1;
}

static char *lr_next(line_reader_t *r)
{
    for (;;) {
        char *start = r->buf + r->pos;
        char *nl = memchr(start, '\n', r->len - r->pos);
        if (nl) {
            *nl = '\0';
            r->pos = (size_t)(nl - r->buf) + 1;
            if (r->skipping) {
                r->skipping = 0;
                continue;
            }
            if (nl > start && nl[-1] == '\r') nl[-1] = '\0';
            return start;
        }
        if (r->eof) {
            if (r->pos >= r->len || r->skipping) {
                r->pos = r->len;
                return NULL;
            }
            r->buf[r->len] = '\0'; /* a last line without its line end */
            r->pos = r->len;
            const size_t n = strlen(start);
            if (n > 0 && start[n - 1] == '\r') start[n - 1] = '\0';
            return start;
        }
        size_t rest = r->len - r->pos;
        if (rest == LR_SIZE) { /* the whole buffer and no line end */
            r->skipping = 1;
            rest = 0;
        }
        memmove(r->buf, r->buf + r->pos, rest);
        r->len = rest;
        r->pos = 0;
        const ssize_t n = read(r->fd, r->buf + r->len, LR_SIZE - r->len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) r->eof = 1; /* an error reads as the end of the file */
        else r->len += (size_t)n;
    }
}

/* ----------------------------------------------------------------------------------------------- /proc/net/tcp(6) */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* n hex digits (1..8) */
static int hex_n(const char *s, size_t n, uint32_t *out)
{
    if (n < 1 || n > 8) return 0;
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) {
        const int d = hexval(s[i]);
        if (d < 0) return 0;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return 1;
}

/* n decimal digits (1..20) */
static int dec_n(const char *s, size_t n, unsigned long long *out)
{
    if (n < 1 || n > 20) return 0;
    unsigned long long v = 0;
    for (size_t i = 0; i < n; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        const unsigned long long d = (unsigned long long)(s[i] - '0');
        if (v > (~0ULL - d) / 10) return 0;
        v = v * 10 + d;
    }
    *out = v;
    return 1;
}

/* "0100007F:0016" (tcp) or 32 hex digits + ":" + port (tcp6); returns the family (4 / 6) or 0 */
static int parse_endpoint(const char *tok, size_t len, uint32_t words[4], uint16_t *port)
{
    const char *colon = memchr(tok, ':', len);
    if (!colon) return 0;
    const size_t addr_len = (size_t)(colon - tok);
    const size_t port_len = len - addr_len - 1;
    if (addr_len != 8 && addr_len != 32) return 0;
    for (size_t i = 0; i < addr_len / 8; ++i)
        if (!hex_n(tok + 8 * i, 8, &words[i])) return 0; /* the in-memory __be32, printed as a native u32 */
    uint32_t p;
    if (port_len > 4 || !hex_n(colon + 1, port_len, &p)) return 0;
    *port = (uint16_t)p;
    return addr_len == 8 ? 4 : 6;
}

int cp0_ssh_parse_tcp_line(const char *line, cp0_ssh_sock_t *out)
{
    if (!line || !out) return 0;
    /* "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ..." */
    enum { SL, LOCAL, REMOTE, ST, QUEUES, TIMER, RETRANSMIT, UID, TIMEOUT, INODE, FIELDS };
    const char *tok[FIELDS];
    size_t len[FIELDS];
    int n = 0;
    const char *p = line;
    while (n < FIELDS) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        tok[n] = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        len[n] = (size_t)(p - tok[n]);
        ++n;
    }
    if (n < FIELDS) return 0;

    cp0_ssh_sock_t s;
    memset(&s, 0, sizeof(s));
    unsigned long long number;
    if (len[SL] < 2 || tok[SL][len[SL] - 1] != ':' || !dec_n(tok[SL], len[SL] - 1, &number)) return 0;
    const int local_family = parse_endpoint(tok[LOCAL], len[LOCAL], s.local, &s.local_port);
    const int remote_family = parse_endpoint(tok[REMOTE], len[REMOTE], s.remote, &s.remote_port);
    if (!local_family || local_family != remote_family) return 0;
    s.family = (uint8_t)local_family;
    uint32_t value;
    if (len[ST] != 2 || !hex_n(tok[ST], 2, &value)) return 0;
    s.state = (uint8_t)value;
    const char *colon = memchr(tok[QUEUES], ':', len[QUEUES]);
    if (!colon) return 0;
    const size_t tx_len = (size_t)(colon - tok[QUEUES]);
    if (!hex_n(tok[QUEUES], tx_len, &s.tx_queue) || !hex_n(colon + 1, len[QUEUES] - tx_len - 1, &s.rx_queue)) return 0;
    if (!dec_n(tok[UID], len[UID], &number) || number > 0xFFFFFFFFULL) return 0;
    s.uid = (uint32_t)number;
    if (!dec_n(tok[INODE], len[INODE], &number)) return 0;
    s.inode = (unsigned long)number;
    *out = s;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ sshd's ports */

typedef struct {
    uint16_t *ports;
    int max;
    int n;
    int port_lines; /* `Port` lines seen (none: sshd's default 22) */
} port_set_t;

static void add_port(port_set_t *set, unsigned long long port)
{
    if (port < 1 || port > 65535) return;
    for (int i = 0; i < set->n; ++i)
        if (set->ports[i] == port) return;
    if (set->n < set->max) set->ports[set->n++] = (uint16_t)port;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

/* a decimal port token, the whole of [s, s + n) */
static int port_value(const char *s, size_t n, unsigned long long *port)
{
    return dec_n(s, n, port) && *port >= 1 && *port <= 65535;
}

/* `ListenAddress` argument: "host", "host:port", "[v6]:port", a bare IPv6 address (several colons: no port) */
static void listen_address_port(const char *arg, size_t len, port_set_t *set)
{
    unsigned long long port;
    if (len > 0 && arg[0] == '[') {
        const char *close = memchr(arg, ']', len);
        if (!close || (size_t)(close - arg) + 2 > len || close[1] != ':') return;
        const size_t at = (size_t)(close - arg) + 2;
        if (port_value(arg + at, len - at, &port)) add_port(set, port);
        return;
    }
    const char *colon = memchr(arg, ':', len);
    if (!colon || memchr(colon + 1, ':', len - (size_t)(colon - arg) - 1)) return;
    const size_t at = (size_t)(colon - arg) + 1;
    if (port_value(arg + at, len - at, &port)) add_port(set, port);
}

static int config_file(const char *root, const char *path, port_set_t *set, int depth);

/* root + pattern with the root's glob characters escaped; relative patterns are under /etc/ssh (as sshd reads them) */
static void config_include(const char *root, const char *pattern, port_set_t *set, int depth)
{
    if (depth > INCLUDE_DEPTH_MAX) return;
    char full[640];
    size_t at = 0;
    for (const char *c = root; *c; ++c) {
        if (at + 2 >= sizeof(full)) return;
        if (*c == '*' || *c == '?' || *c == '[' || *c == '\\') full[at++] = '\\';
        full[at++] = *c;
    }
    const int n = snprintf(full + at, sizeof(full) - at, "%s%s", pattern[0] == '/' ? "" : "/etc/ssh/", pattern);
    if (n < 0 || (size_t)n >= sizeof(full) - at) return;
    glob_t found;
    memset(&found, 0, sizeof(found));
    if (glob(full, 0, NULL, &found) == 0)
        for (size_t i = 0; i < found.gl_pathc; ++i) config_file(root, found.gl_pathv[i], set, depth);
    globfree(&found);
}

static void config_line(const char *root, const char *line, port_set_t *set, int depth)
{
    const char *p = line;
    while (is_space(*p)) ++p;
    if (!*p || *p == '#') return;
    const char *keyword = p;
    while (*p && !is_space(*p) && *p != '=') ++p;
    const size_t keyword_len = (size_t)(p - keyword);
    while (is_space(*p)) ++p;
    if (*p == '=') {
        ++p;
        while (is_space(*p)) ++p;
    }
    const int is_port = keyword_len == 4 && strncasecmp(keyword, "Port", 4) == 0;
    const int is_listen = keyword_len == 13 && strncasecmp(keyword, "ListenAddress", 13) == 0;
    const int is_include = keyword_len == 7 && strncasecmp(keyword, "Include", 7) == 0;
    if (!is_port && !is_listen && !is_include) return;
    for (int arg = 0; *p && *p != '#'; ++arg) { /* a token starting with '#' starts a comment */
        const char *start = p;
        while (*p && !is_space(*p)) ++p;
        const size_t len = (size_t)(p - start);
        if (is_port && arg == 0) {
            unsigned long long port;
            ++set->port_lines;
            if (port_value(start, len, &port)) add_port(set, port);
        } else if (is_listen && arg == 0) {
            listen_address_port(start, len, set);
        } else if (is_include) {
            char pattern[256];
            if (len < sizeof(pattern)) {
                memcpy(pattern, start, len);
                pattern[len] = '\0';
                config_include(root, pattern, set, depth + 1);
            }
        }
        while (is_space(*p)) ++p;
    }
}

/* 0: the file cannot be opened */
static int config_file(const char *root, const char *path, port_set_t *set, int depth)
{
    line_reader_t reader;
    if (!lr_open(&reader, path)) return 0;
    const char *line;
    while ((line = lr_next(&reader))) config_line(root, line, set, depth);
    lr_close(&reader);
    return 1;
}

int cp0_ssh_config_ports(const char *root, uint16_t *ports, int max)
{
    if (!ports || max <= 0) return 0;
    if (!root) root = "";
    port_set_t set = {ports, max, 0, 0};
    const char *env = getenv(CP0_SSH_PORTS_ENV);
    if (env && env[0]) {
        const char *p = env;
        while (*p) {
            while (*p == ',' || *p == ';' || is_space(*p)) ++p;
            const char *start = p;
            while (*p && *p != ',' && *p != ';' && !is_space(*p)) ++p;
            unsigned long long port;
            if (p > start && port_value(start, (size_t)(p - start), &port)) add_port(&set, port);
        }
        if (set.n > 0) return set.n;
    }
    char path[512];
    const int n = snprintf(path, sizeof(path), "%s/etc/ssh/sshd_config", root);
    if (n < 0 || (size_t)n >= sizeof(path) || !config_file(root, path, &set, 0))
        config_include(root, "/etc/ssh/sshd_config.d/*.conf", &set, 1); /* Debian's default Include */
    if (set.port_lines == 0) add_port(&set, 22);
    if (set.n == 0) add_port(&set, 22); /* only invalid `Port` values: sshd would refuse them; keep the default */
    return set.n;
}

/* ---------------------------------------------------------------------------------------------- netlink counters */

#if defined(__linux__)

static int conn_for(const cp0_ssh_conn_t *conns, int count, int family, const struct inet_diag_msg *m)
{
    const uint16_t sport = ntohs(m->id.idiag_sport);
    const uint16_t dport = ntohs(m->id.idiag_dport);
    const int want = family == AF_INET ? 4 : 6;
    const int words = family == AF_INET ? 1 : 4;
    for (int i = 0; i < count; ++i) {
        const cp0_ssh_sock_t *s = &conns[i].sock;
        if (s->family != want || s->local_port != sport || s->remote_port != dport) continue;
        int same = 1;
        for (int w = 0; w < words && same; ++w)
            same = s->local[w] == m->id.idiag_src[w] && s->remote[w] == m->id.idiag_dst[w];
        if (same) return i;
    }
    return -1;
}

/* One reply message: 0, or -1 with errno for a matched connection whose tcp_info is missing or has no byte counters */
static int take_reply(const char *data, size_t data_len, int family, cp0_ssh_conn_t *conns, int count)
{
    struct inet_diag_msg msg;
    memcpy(&msg, data, sizeof(msg));
    const int i = conn_for(conns, count, family, &msg);
    if (i < 0) return 0;
    size_t off = NLMSG_ALIGN(sizeof(struct inet_diag_msg));
    const size_t header = RTA_ALIGN(sizeof(struct rtattr));
    while (off + sizeof(struct rtattr) <= data_len) {
        struct rtattr attr;
        memcpy(&attr, data + off, sizeof(attr));
        if (attr.rta_len < sizeof(attr) || attr.rta_len > data_len - off) break;
        if (attr.rta_type == INET_DIAG_INFO) {
            const size_t payload = attr.rta_len - header;
            struct tcp_info info;
            const size_t need = offsetof(struct tcp_info, tcpi_bytes_received) + sizeof(info.tcpi_bytes_received);
            if (attr.rta_len < header || payload < need) break; /* a kernel before 4.1 */
            memset(&info, 0, sizeof(info));
            memcpy(&info, data + off + header, payload < sizeof(info) ? payload : sizeof(info));
            conns[i].bytes = (uint64_t)info.tcpi_bytes_acked + (uint64_t)info.tcpi_bytes_received;
            conns[i].has_bytes = 1;
            return 0;
        }
        off += RTA_ALIGN(attr.rta_len);
    }
    errno = ENOTSUP;
    return -1;
}

static int dump_family(int fd, int family, uint32_t seq, cp0_ssh_conn_t *conns, int count)
{
    struct {
        struct nlmsghdr nlh;
        struct inet_diag_req_v2 req;
    } request;
    memset(&request, 0, sizeof(request));
    request.nlh.nlmsg_len = sizeof(request);
    request.nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    request.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.nlh.nlmsg_seq = seq;
    request.req.sdiag_family = (uint8_t)family;
    request.req.sdiag_protocol = IPPROTO_TCP;
    request.req.idiag_ext = 1u << (INET_DIAG_INFO - 1);
    request.req.idiag_states = 1u << CP0_SSH_TCP_ESTABLISHED;
    struct sockaddr_nl kernel;
    memset(&kernel, 0, sizeof(kernel));
    kernel.nl_family = AF_NETLINK;
    if (sendto(fd, &request, sizeof(request), 0, (struct sockaddr *)&kernel, sizeof(kernel)) != (ssize_t)sizeof(request))
        return -1;

    const int64_t deadline = cp0_ssh_monotonic_ms() + CP0_SSH_DIAG_TIMEOUT_MS;
    long buf[8192 / sizeof(long)]; /* the kernel sizes its dump batches to the reader's buffer: none is truncated */
    for (;;) {
        struct iovec iov = {buf, sizeof(buf)};
        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        const ssize_t got = recvmsg(fd, &mh, MSG_DONTWAIT);
        if (got < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
            const int64_t left = deadline - cp0_ssh_monotonic_ms();
            struct pollfd pfd = {fd, POLLIN, 0};
            const int ready = left > 0 ? poll(&pfd, 1, (int)left) : 0;
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) return -1;
            if (ready == 0) {
                errno = ETIMEDOUT;
                return -1;
            }
            continue;
        }
        if (got == 0) {
            errno = EPROTO;
            return -1;
        }
        if (mh.msg_flags & MSG_TRUNC) {
            errno = EMSGSIZE;
            return -1;
        }
        const char *base = (const char *)buf;
        size_t off = 0;
        while (off + sizeof(struct nlmsghdr) <= (size_t)got) {
            struct nlmsghdr h;
            memcpy(&h, base + off, sizeof(h));
            if (h.nlmsg_len < sizeof(h) || h.nlmsg_len > (size_t)got - off) break;
            const char *data = base + off + NLMSG_HDRLEN;
            const size_t data_len = h.nlmsg_len - NLMSG_HDRLEN;
            off += NLMSG_ALIGN(h.nlmsg_len);
            if (h.nlmsg_seq != seq) continue;
            if (h.nlmsg_type == NLMSG_DONE) return 0;
            if (h.nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr err;
                int code = EPROTO;
                if (data_len >= sizeof(err)) {
                    memcpy(&err, data, sizeof(err));
                    if (err.error < 0) code = -err.error;
                }
                errno = code;
                return -1;
            }
            if (h.nlmsg_type != SOCK_DIAG_BY_FAMILY || data_len < sizeof(struct inet_diag_msg)) continue;
            if (take_reply(data, data_len, family, conns, count) != 0) return -1;
        }
    }
}

int cp0_ssh_diag_counters(void *ctx, cp0_ssh_conn_t *conns, int count)
{
    (void)ctx;
    if (!conns || count <= 0) return 0;
    int want4 = 0, want6 = 0;
    for (int i = 0; i < count; ++i) {
        conns[i].has_bytes = 0;
        if (conns[i].sock.family == 4) want4 = 1;
        else if (conns[i].sock.family == 6) want6 = 1;
    }
    const int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_SOCK_DIAG);
    if (fd < 0) return -1;
    int rc = 0;
    if (want4) rc = dump_family(fd, AF_INET, 1, conns, count);
    if (rc == 0 && want6) rc = dump_family(fd, AF_INET6, 2, conns, count);
    const int saved = errno;
    close(fd);
    errno = saved;
    return rc;
}

#else

int cp0_ssh_diag_counters(void *ctx, cp0_ssh_conn_t *conns, int count)
{
    (void)ctx;
    (void)conns;
    (void)count;
    errno = ENOTSUP;
    return -1;
}

#endif

/* ----------------------------------------------------------------------------------------------------- the watch */

int64_t cp0_ssh_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void cp0_ssh_watch_init(cp0_ssh_watch_t *w, const cp0_ssh_watch_options_t *options)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    const char *root = options ? options->root : NULL;
    if (!root) root = getenv(CP0_SSH_ROOT_ENV);
    if (!root) root = "";
    snprintf(w->root, sizeof(w->root), "%s", root);
    w->counters = options ? options->counters : NULL;
    w->counters_ctx = options ? options->counters_ctx : NULL;
    w->use_netlink = !w->counters && !w->root[0];
}

void cp0_ssh_watch_reload(cp0_ssh_watch_t *w)
{
    if (w) w->ports_loaded = 0;
}

static int port_index(const cp0_ssh_watch_t *w, uint16_t port)
{
    for (int i = 0; i < w->nports; ++i)
        if (w->ports[i] == port) return i;
    return -1;
}

static int same_conn(const cp0_ssh_sock_t *a, const cp0_ssh_sock_t *b)
{
    return a->family == b->family && a->local_port == b->local_port && a->remote_port == b->remote_port &&
           memcmp(a->local, b->local, sizeof(a->local)) == 0 && memcmp(a->remote, b->remote, sizeof(a->remote)) == 0;
}

#define CANDIDATES_MAX (2 * CP0_SSH_MAX_CONNS)

/* The LISTEN ports (bits of w->ports) and the ESTABLISHED sockets on a config port, from one /proc file */
static void scan(const cp0_ssh_watch_t *w, const char *rel, int family, cp0_ssh_sock_t *cand, int *ncand,
                 unsigned *listening)
{
    char path[512];
    const int n = snprintf(path, sizeof(path), "%s%s", w->root, rel);
    if (n < 0 || (size_t)n >= sizeof(path)) return;
    line_reader_t reader;
    if (!lr_open(&reader, path)) return;
    const char *line;
    cp0_ssh_sock_t s;
    while ((line = lr_next(&reader))) {
        if (!cp0_ssh_parse_tcp_line(line, &s) || s.family != family) continue;
        const int i = port_index(w, s.local_port);
        if (i < 0) continue;
        if (s.state == CP0_SSH_TCP_LISTEN) *listening |= 1u << i;
        else if (s.state == CP0_SSH_TCP_ESTABLISHED && *ncand < CANDIDATES_MAX) cand[(*ncand)++] = s;
    }
    lr_close(&reader);
}

/* now - then has reached period (or the clock went back) */
static int period_over(int64_t now, int64_t then, int64_t period)
{
    const int64_t d = now - then;
    return d < 0 || d >= period;
}

int cp0_ssh_watch_sample(cp0_ssh_watch_t *w, int64_t now_ms, cp0_ssh_status_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!w) return 0;
    if (!w->ports_loaded || period_over(now_ms, w->ports_read_ms, CP0_SSH_CONFIG_PERIOD_MS)) {
        w->nports = cp0_ssh_config_ports(w->root, w->ports, CP0_SSH_MAX_PORTS);
        w->ports_loaded = 1;
        w->ports_read_ms = now_ms;
    }

    cp0_ssh_sock_t cand[CANDIDATES_MAX];
    int ncand = 0;
    unsigned listening = 0;
    scan(w, "/proc/net/tcp", 4, cand, &ncand, &listening);
    scan(w, "/proc/net/tcp6", 6, cand, &ncand, &listening);
    for (int i = 0; i < w->nports; ++i)
        if (listening & (1u << i)) ++out->listening;

    /* this sample's table: carried over by identity, confirmed per CONFIRMATION */
    cp0_ssh_conn_t next[CP0_SSH_MAX_CONNS];
    uint64_t prev_bytes[CP0_SSH_MAX_CONNS];
    uint8_t prev_has[CP0_SSH_MAX_CONNS];
    int nn = 0;
    for (int c = 0; c < ncand && nn < CP0_SSH_MAX_CONNS; ++c) {
        const int port = port_index(w, cand[c].local_port);
        if (port < 0 || !(listening & (1u << port))) continue;
        cp0_ssh_conn_t *conn = &next[nn];
        memset(conn, 0, sizeof(*conn));
        conn->sock = cand[c];
        conn->first_seen_ms = now_ms;
        prev_bytes[nn] = 0;
        prev_has[nn] = 0;
        for (int p = 0; p < w->nconns; ++p) { /* in the previous sample: present in every sample since first seen */
            if (!same_conn(&w->conns[p].sock, &cand[c])) continue;
            const int64_t age = now_ms - w->conns[p].first_seen_ms;
            if (age >= 0) conn->first_seen_ms = w->conns[p].first_seen_ms; /* else the clock went back: start over */
            conn->counted = age >= CP0_SSH_CONFIRM_MS;
            prev_bytes[nn] = w->conns[p].bytes;
            prev_has[nn] = w->conns[p].has_bytes;
            break;
        }
        ++nn;
    }

    if (nn > 0) {
        cp0_ssh_counters_fn counters = w->counters ? w->counters : (w->use_netlink ? cp0_ssh_diag_counters : NULL);
        int rc = -1;
        if (counters && (!w->diag_failed || period_over(now_ms, w->diag_retry_ms, CP0_SSH_DIAG_RETRY_MS))) {
            rc = counters(w->counters_ctx, next, nn);
            w->diag_failed = rc != 0;
            if (rc != 0) w->diag_retry_ms = now_ms;
        }
        out->source = rc == 0 ? CP0_SSH_ACT_BYTES : CP0_SSH_ACT_QUEUE;
        for (int i = 0; i < nn; ++i) {
            if (rc != 0) next[i].has_bytes = 0;
            if (!next[i].counted) continue;
            ++out->count;
            if (rc == 0) {
                if (next[i].has_bytes && prev_has[i] && next[i].bytes > prev_bytes[i] &&
                    next[i].bytes - prev_bytes[i] >= CP0_SSH_ACTIVE_MIN_BYTES)
                    out->active = 1;
            } else if (next[i].sock.tx_queue || next[i].sock.rx_queue) {
                out->active = 1;
            }
        }
    }
    memcpy(w->conns, next, sizeof(next[0]) * (size_t)nn);
    w->nconns = nn;
    return out->count;
}

static int v4_mapped(const uint32_t a[4]) { return a[0] == 0 && a[1] == 0 && a[2] == htonl(0x0000FFFFu); }

int cp0_ssh_watch_peers(const cp0_ssh_watch_t *w, cp0_ssh_peer_t *peers, int max)
{
    if (!w || !peers || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < w->nconns && n < max; ++i) {
        const cp0_ssh_sock_t *s = &w->conns[i].sock;
        if (!w->conns[i].counted) continue;
        cp0_ssh_peer_t *peer = &peers[n++];
        memset(peer, 0, sizeof(*peer));
        peer->port = s->remote_port;
        const void *addr = s->family == 4 ? (const void *)&s->remote[0]
                           : v4_mapped(s->remote) ? (const void *)&s->remote[3] : (const void *)s->remote;
        const int family = s->family == 4 || v4_mapped(s->remote) ? AF_INET : AF_INET6;
        if (!inet_ntop(family, addr, peer->addr, sizeof(peer->addr))) peer->addr[0] = '\0';
    }
    return n;
}
