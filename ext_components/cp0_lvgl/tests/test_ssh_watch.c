/*
 * SPDX-License-Identifier: MIT
 *
 * SSH indicator P1: the incoming SSH watch (cp0_ssh_watch.h) on fake trees and on real loopback sockets.
 *   - /proc/net/tcp(6) lines: the kernel's spelling (literal lines, little-endian hosts), the header, malformed and
 *     truncated lines;
 *   - sshd's ports: Port / ListenAddress / Include (absolute, relative, a loop), comments, `Keyword=value`, case,
 *     invalid values, an unreadable or missing sshd_config, the Debian .d default, $APPLAUNCH_SSH_PORTS;
 *   - detection: IPv4, IPv6, v4-mapped in tcp6, loopback, two sessions, the LISTEN filter, an outgoing client, other TCP
 *     states, several sshd ports, the env override, $APPLAUNCH_STATUS_SYSFS_ROOT, the tracking cap;
 *   - confirmation: seen once, a fast caller, a missed sample;
 *   - activity: byte counters through the counters hook (baseline, delta, idle, no baseline, unconfirmed connections),
 *     the queue fallback, the netlink retry delay;
 *   - malformed files: junk, binary bytes, an over-long line, a truncated last line, CRLF, empty and missing files;
 *   - real loopback sockets (IPv4, and IPv6 dual-stack with a v4-mapped peer): count after two samples, activity while
 *     bytes move and not after, the client side never counts, closing drops the count. NETLINK_SOCK_DIAG unavailable:
 *     the activity part prints SKIP (REQUIRE_SOCK_DIAG=1 makes that a failure).
 */
#define _DEFAULT_SOURCE /* mkdtemp, setenv */
#include "cp0_ssh_watch.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char g_base[256];
static int g_tree;

/* ------------------------------------------------------------------------------------------------------- helpers */

static void new_tree(char *root, size_t size)
{
    snprintf(root, size, "%s/tree%d", g_base, ++g_tree);
    assert(mkdir(root, 0755) == 0);
}

/* root/rel = bytes (parent directories made on demand) */
static void put_bytes(const char *root, const char *rel, const char *bytes, size_t len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", root, rel);
    for (char *slash = path + strlen(root) + 1; (slash = strchr(slash, '/')); ++slash) {
        *slash = '\0';
        if (mkdir(path, 0755) != 0) assert(errno == EEXIST);
        *slash = '/';
    }
    FILE *out = fopen(path, "wb");
    assert(out);
    assert(fwrite(bytes, 1, len, out) == len);
    fclose(out);
}

static void put(const char *root, const char *rel, const char *text) { put_bytes(root, rel, text, strlen(text)); }

typedef struct {
    char text[32768];
    size_t len;
    int lines;
} text_t;

static void tput(text_t *t, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(t->text + t->len, sizeof(t->text) - t->len, format, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(t->text) - t->len);
    t->len += (size_t)n;
}

static void t_init(text_t *t, int family)
{
    t->len = 0;
    t->lines = 0;
    t->text[0] = '\0';
    if (family == 4)
        tput(t, "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n");
    else
        tput(t, "  sl  local_address                         remote_address                        st tx_queue rx_queue tr "
                "tm->when retrnsmt   uid  timeout inode\n");
}

static uint32_t v4(const char *ip)
{
    struct in_addr a;
    assert(inet_pton(AF_INET, ip, &a) == 1);
    uint32_t word;
    memcpy(&word, &a, sizeof(word));
    return word;
}

static void v6(const char *ip, uint32_t words[4])
{
    struct in6_addr a;
    assert(inet_pton(AF_INET6, ip, &a) == 1);
    memcpy(words, &a, 16);
}

/* one /proc/net/tcp line as the kernel prints it (get_tcp4_sock): each address is its __be32 printed as a u32 */
static void add4(text_t *t, const char *lip, int lport, const char *rip, int rport, int st, unsigned tx, unsigned rx)
{
    tput(t, "%4d: %08X:%04X %08X:%04X %02X %08X:%08X 00:00000000 00000000 %5u        0 %lu 1 0000000000000000 20 4 30 "
            "10 -1\n",
         t->lines, (unsigned)v4(lip), lport, (unsigned)v4(rip), rport, st, tx, rx, 0u, 20000ul + (unsigned long)t->lines);
    ++t->lines;
}

/* one /proc/net/tcp6 line (get_tcp6_sock: the four s6_addr32 words) */
static void add6(text_t *t, const char *lip, int lport, const char *rip, int rport, int st, unsigned tx, unsigned rx)
{
    uint32_t l[4], r[4];
    v6(lip, l);
    v6(rip, r);
    tput(t, "%4d: %08X%08X%08X%08X:%04X %08X%08X%08X%08X:%04X %02X %08X:%08X 00:00000000 00000000 %5u        0 %lu 1 "
            "0000000000000000 20 4 30 10 -1\n",
         t->lines, (unsigned)l[0], (unsigned)l[1], (unsigned)l[2], (unsigned)l[3], lport, (unsigned)r[0], (unsigned)r[1],
         (unsigned)r[2], (unsigned)r[3], rport, st, tx, rx, 0u, 30000ul + (unsigned long)t->lines);
    ++t->lines;
}

#define EST CP0_SSH_TCP_ESTABLISHED
#define LST CP0_SSH_TCP_LISTEN

typedef struct {
    char root[300];
    text_t t4, t6;
} fixture_t;

static void fx_init(fixture_t *f)
{
    new_tree(f->root, sizeof(f->root));
    t_init(&f->t4, 4);
    t_init(&f->t6, 6);
}

static void fx_clear(fixture_t *f)
{
    t_init(&f->t4, 4);
    t_init(&f->t6, 6);
}

static void fx_write(fixture_t *f)
{
    put(f->root, "proc/net/tcp", f->t4.text);
    put(f->root, "proc/net/tcp6", f->t6.text);
}

/* the counters hook: bytes per remote (client) port */
typedef struct {
    int calls;
    int fail;
    int n;
    struct {
        uint16_t remote_port;
        uint64_t bytes;
    } table[8];
} fake_counters_t;

static int fake_counters(void *ctx, cp0_ssh_conn_t *conns, int count)
{
    fake_counters_t *f = ctx;
    ++f->calls;
    if (f->fail) return -1;
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < f->n; ++k)
            if (f->table[k].remote_port == conns[i].sock.remote_port) {
                conns[i].bytes = f->table[k].bytes;
                conns[i].has_bytes = 1;
            }
    return 0;
}

static void fake_set(fake_counters_t *f, uint16_t remote_port, uint64_t bytes)
{
    for (int k = 0; k < f->n; ++k)
        if (f->table[k].remote_port == remote_port) {
            f->table[k].bytes = bytes;
            return;
        }
    assert(f->n < 8);
    f->table[f->n].remote_port = remote_port;
    f->table[f->n].bytes = bytes;
    ++f->n;
}

static void watch_at(cp0_ssh_watch_t *w, const char *root, fake_counters_t *fake)
{
    cp0_ssh_watch_options_t options = {root, fake ? fake_counters : NULL, fake};
    cp0_ssh_watch_init(w, &options);
}

/* the count after two samples 2 s apart (a fresh watch, the queue fallback under the root) */
static int count_after_two(const char *root, cp0_ssh_status_t *last)
{
    cp0_ssh_watch_t w;
    cp0_ssh_status_t s;
    watch_at(&w, root, NULL);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0); /* first sighting never counts */
    assert(s.count == 0 && s.active == 0);
    const int count = cp0_ssh_watch_sample(&w, 2000, &s);
    assert(count == s.count);
    if (last) *last = s;
    return count;
}

static int has_port(const uint16_t *ports, int n, int port)
{
    for (int i = 0; i < n; ++i)
        if (ports[i] == port) return 1;
    return 0;
}

/* -------------------------------------------------------------------------------------------------- line parsing */

static void test_parse_line(void)
{
    cp0_ssh_sock_t s;
    memset(&s, 0x5a, sizeof(s));
    assert(cp0_ssh_parse_tcp_line(NULL, &s) == 0);
    assert(cp0_ssh_parse_tcp_line("", &s) == 0);
    assert(cp0_ssh_parse_tcp_line("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  "
                                  "timeout inode",
                                  &s) == 0);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    /* literal kernel lines (WSL 5.15 and a v4-mapped sshd connection): the words are little-endian u32 prints */
    assert(cp0_ssh_parse_tcp_line("   0: 3600007F:0035 00000000:0000 0A 00000000:00000000 00:00000000 00000000   991  "
                                  "      0 17658 1 0000000000000000 100 0 0 10 5",
                                  &s) == 1);
    assert(s.family == 4 && s.state == LST && s.local_port == 53 && s.remote_port == 0);
    assert(s.local[0] == v4("127.0.0.54") && s.remote[0] == 0 && s.uid == 991 && s.inode == 17658);

    assert(cp0_ssh_parse_tcp_line("   1: 0000000000000000FFFF00000100007F:0016 0000000000000000FFFF00000C44A8C0:D3A2 01 "
                                  "00000000:00000040 02:000A5F2B 00000000     0        0 31337 2 0000000000000000 20 4 30 "
                                  "10 -1",
                                  &s) == 1);
    uint32_t mapped[4];
    v6("::ffff:192.168.68.12", mapped);
    assert(s.family == 6 && s.state == EST && s.local_port == 22 && s.remote_port == 54178);
    assert(memcmp(s.remote, mapped, sizeof(mapped)) == 0);
    assert(s.tx_queue == 0 && s.rx_queue == 0x40 && s.uid == 0 && s.inode == 31337);
#endif

    /* the test's own spelling round-trips */
    text_t t;
    t_init(&t, 4);
    t.len = 0;
    add4(&t, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 5, 7);
    t.text[t.len - 1] = '\0';
    assert(cp0_ssh_parse_tcp_line(t.text, &s) == 1);
    assert(s.family == 4 && s.local[0] == v4("192.168.68.50") && s.remote[0] == v4("192.168.68.12"));
    assert(s.local_port == 22 && s.remote_port == 51000 && s.tx_queue == 5 && s.rx_queue == 7);

    /* malformed: each one field broken */
    const char *bad[] = {
        "   0: 0100007F:0016",                                                                      /* truncated */
        "   0: 0100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000        0", /* no inode */
        "   0: 100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123",   /* 7 digits */
        "   0: 0100007G:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123",  /* not hex */
        "   0: 0100007F:00016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123", /* 5-digit port */
        "   0: 0100007F0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123",   /* no colon */
        "   0: 0100007F:0016 00000000000000000000000000000000:D431 01 00000000:00000000 00:0 0 1000 0 123", /* mixed */
        "   0: 0100007F:0016 0100007F:D431 1 00000000:00000000 00:00000000 00000000  1000 0 123",   /* 1-digit st */
        "   0: 0100007F:0016 0100007F:D431 01 0000000000000000 00:00000000 00000000  1000 0 123",   /* queues */
        "   0: 0100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  10x0 0 123",  /* uid */
        "   0: 0100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 12a",  /* inode */
        "   x: 0100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123",  /* sl */
        "   0: 0100007F: 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 123",      /* empty port */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        if (cp0_ssh_parse_tcp_line(bad[i], &s) != 0) {
            fprintf(stderr, "accepted malformed line %zu: %s\n", i, bad[i]);
            assert(0);
        }
    }
    assert(cp0_ssh_parse_tcp_line("   0: 0100007F:0016 0100007F:D431 01 00000000:00000000 00:00000000 00000000  1000 0 "
                                  "123",
                                  &s) == 1);
    assert(cp0_ssh_parse_tcp_line("x", NULL) == 0);
}

/* --------------------------------------------------------------------------------------------------- sshd's ports */

static void test_config_ports(void)
{
    char root[300];
    uint16_t ports[CP0_SSH_MAX_PORTS];
    int n;

    /* nothing: sshd's default */
    new_tree(root, sizeof(root));
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 22);
    assert(cp0_ssh_config_ports(root, ports, 0) == 0);
    assert(cp0_ssh_config_ports(root, NULL, 4) == 0);

    /* one Port line replaces the default */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Port 2222\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 2222);

    /* comments, case, '=', a trailing comment, invalid values, the .d files of an absolute Include, a non-.conf file */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config",
        "# Port 2000\n"
        "   #Port 2001\n"
        "port 22\n"
        "  PORT=2201\n"
        "Port = 2202 # the second one\n"
        "Port abc\n"
        "Port 70000\n"
        "Port 0\n"
        "Port\n"
        "PortX 2003\n"
        "Include /etc/ssh/sshd_config.d/*.conf\n"
        "PasswordAuthentication no\r\n"
        "Port 2203\r\n");
    put(root, "etc/ssh/sshd_config.d/10-port.conf", "Port 2222\n");
    put(root, "etc/ssh/sshd_config.d/20-listen.conf",
        "ListenAddress [::]:2300\nListenAddress 0.0.0.0:2400\nListenAddress ::\nListenAddress 192.168.1.5\n"
        "ListenAddress [fe80::1]\nListenAddress 10.0.0.1:99999\n");
    put(root, "etc/ssh/sshd_config.d/README", "Port 9999\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 7);
    assert(has_port(ports, n, 22) && has_port(ports, n, 2201) && has_port(ports, n, 2202) && has_port(ports, n, 2203));
    assert(has_port(ports, n, 2222) && has_port(ports, n, 2300) && has_port(ports, n, 2400));
    assert(!has_port(ports, n, 2000) && !has_port(ports, n, 2001) && !has_port(ports, n, 2003));
    assert(!has_port(ports, n, 9999));

    /* a relative Include (under /etc/ssh, as sshd reads it), several patterns on one line, a missing one */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Include sshd_config.d/*.conf /etc/ssh/extra.conf /etc/ssh/missing/*.conf\n");
    put(root, "etc/ssh/sshd_config.d/a.conf", "Port 2500\n");
    put(root, "etc/ssh/extra.conf", "Port 2501\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 2 && has_port(ports, n, 2500) && has_port(ports, n, 2501));

    /* ListenAddress ports only, no Port line: sshd's default Port 22 still applies to address-only lines */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "ListenAddress [::]:2600\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 2 && has_port(ports, n, 2600) && has_port(ports, n, 22));

    /* only invalid Port values: the default stays */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Port nope\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 22);

    /* an Include loop ends at the depth limit */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Include /etc/ssh/loop.conf\n");
    put(root, "etc/ssh/loop.conf", "Port 2700\nInclude /etc/ssh/loop.conf\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 2700);

    /* no sshd_config: the Debian default Include */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config.d/port.conf", "Port 2800\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 2800);

    /* an unreadable sshd_config: the same (root reads anything: then only the readable case is checked) */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Port 2900\n");
    put(root, "etc/ssh/sshd_config.d/port.conf", "Port 2901\n");
    char path[400];
    snprintf(path, sizeof(path), "%s/etc/ssh/sshd_config", root);
    assert(chmod(path, 0) == 0);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    if (geteuid() != 0) assert(n == 1 && ports[0] == 2901);
    assert(chmod(path, 0644) == 0);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 2900); /* readable, without an Include line: the .d files are not read */

    /* a comment longer than the reader's 4096-byte buffer is dropped whole: its tail (cut exactly at the buffer's end
     * here) is never read as a line of its own */
    new_tree(root, sizeof(root));
    static char long_comment[6000];
    memset(long_comment, 'x', 4096);
    long_comment[0] = '#';
    snprintf(long_comment + 4096, sizeof(long_comment) - 4096, "Port 9998 is still the comment\nPort 2950\n");
    put(root, "etc/ssh/sshd_config", long_comment);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 1 && ports[0] == 2950);

    /* the cap */
    new_tree(root, sizeof(root));
    put(root, "etc/ssh/sshd_config", "Port 1\nPort 2\nPort 3\nPort 4\nPort 5\nPort 6\nPort 7\nPort 8\nPort 9\nPort 2\n");
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == CP0_SSH_MAX_PORTS && ports[0] == 1 && ports[7] == 8);
    n = cp0_ssh_config_ports(root, ports, 2);
    assert(n == 2);

    /* $APPLAUNCH_SSH_PORTS replaces the config; unusable: the config */
    assert(setenv(CP0_SSH_PORTS_ENV, "2222, 3333;4444 2222", 1) == 0);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == 3 && ports[0] == 2222 && ports[1] == 3333 && ports[2] == 4444);
    assert(setenv(CP0_SSH_PORTS_ENV, "abc,0,70000", 1) == 0);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == CP0_SSH_MAX_PORTS && ports[0] == 1);
    assert(setenv(CP0_SSH_PORTS_ENV, "", 1) == 0);
    n = cp0_ssh_config_ports(root, ports, CP0_SSH_MAX_PORTS);
    assert(n == CP0_SSH_MAX_PORTS && ports[0] == 1);
    unsetenv(CP0_SSH_PORTS_ENV);
}

/* ------------------------------------------------------------------------------------------------------ detection */

static void test_detection(void)
{
    fixture_t f;
    cp0_ssh_status_t s;
    cp0_ssh_watch_t w;
    cp0_ssh_peer_t peers[4];

    /* IPv4 on 22 with sshd listening: 0 at the first sample, 1 at the second; the peer */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add6(&f.t6, "::", 22, "::", 0, LST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, NULL);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0 && s.listening == 1 && s.source == CP0_SSH_ACT_QUEUE);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 0); /* not confirmed yet */
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.active == 0);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 1);
    assert(strcmp(peers[0].addr, "192.168.68.12") == 0 && peers[0].port == 51000);
    assert(cp0_ssh_watch_peers(&w, peers, 0) == 0 && cp0_ssh_watch_peers(NULL, peers, 4) == 0);

    /* listen only */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, &s) == 0 && s.listening == 1 && s.source == CP0_SSH_ACT_NONE);

    /* sshd not listening: an established socket on 22 does not count */
    fx_init(&f);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, &s) == 0 && s.listening == 0);

    /* the launcher's own outgoing ssh (ephemeral local port, remote 22) */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 51234, "192.168.68.12", 22, EST, 0, 0);
    add6(&f.t6, "2001:db8::50", 51235, "2001:db8::12", 22, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, NULL) == 0);

    /* other TCP states on 22: SYN_RECV, FIN_WAIT1, TIME_WAIT, CLOSE, CLOSE_WAIT, LAST_ACK, CLOSING */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    const int states[] = {0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B};
    for (int i = 0; i < 8; ++i) add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 52000 + i, states[i], 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, NULL) == 0);

    /* v4-mapped in tcp6 (ssh.socket's dual-stack [::]:22, no IPv4 listener): printed as IPv4 */
    fx_init(&f);
    add6(&f.t6, "::", 22, "::", 0, LST, 0, 0);
    add6(&f.t6, "::ffff:192.168.68.50", 22, "::ffff:192.168.68.12", 54178, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, NULL);
    cp0_ssh_watch_sample(&w, 0, &s);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 1);
    assert(strcmp(peers[0].addr, "192.168.68.12") == 0 && peers[0].port == 54178);

    /* a pure IPv6 peer */
    fx_init(&f);
    add6(&f.t6, "::", 22, "::", 0, LST, 0, 0);
    add6(&f.t6, "2001:db8::50", 22, "2001:db8::5", 40001, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, NULL);
    cp0_ssh_watch_sample(&w, 0, &s);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 1 && strcmp(peers[0].addr, "2001:db8::5") == 0);

    /* `ssh localhost`: both ends are in the file, only the server side counts */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "127.0.0.1", 22, "127.0.0.1", 40000, EST, 0, 0);
    add4(&f.t4, "127.0.0.1", 40000, "127.0.0.1", 22, EST, 0, 0);
    add6(&f.t6, "::", 22, "::", 0, LST, 0, 0);
    add6(&f.t6, "::1", 22, "::1", 40002, EST, 0, 0);
    add6(&f.t6, "::1", 40002, "::1", 22, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, NULL);
    cp0_ssh_watch_sample(&w, 0, &s);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 2);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 2);
    assert(strcmp(peers[0].addr, "127.0.0.1") == 0 && peers[0].port == 40000);
    assert(strcmp(peers[1].addr, "::1") == 0 && peers[1].port == 40002);
    assert(cp0_ssh_watch_peers(&w, peers, 1) == 1);

    /* two sessions from the PC */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51001, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, NULL) == 2);

    /* sshd on 2222 (from sshd_config.d): a connection on 22 (another listener) does not count */
    fx_init(&f);
    put(f.root, "etc/ssh/sshd_config", "Include /etc/ssh/sshd_config.d/*.conf\n");
    put(f.root, "etc/ssh/sshd_config.d/port.conf", "Port 2222\n");
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "0.0.0.0", 2222, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add4(&f.t4, "192.168.68.50", 2222, "192.168.68.12", 51001, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, &s) == 1 && s.listening == 1);

    /* several sshd ports, both listening, one connection each; ListenAddress [::]:2200 */
    fx_init(&f);
    put(f.root, "etc/ssh/sshd_config", "Port 22\nPort 2222\nListenAddress [::]:2200\n");
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "0.0.0.0", 2222, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add4(&f.t4, "192.168.68.50", 2222, "192.168.68.12", 51001, EST, 0, 0);
    add6(&f.t6, "::", 2200, "::", 0, LST, 0, 0);
    add6(&f.t6, "2001:db8::50", 2200, "2001:db8::5", 51002, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, &s) == 3 && s.listening == 3);

    /* $APPLAUNCH_SSH_PORTS: only its ports (still LISTEN-filtered) */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "0.0.0.0", 2022, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add4(&f.t4, "192.168.68.50", 2022, "192.168.68.12", 51001, EST, 0, 0);
    add4(&f.t4, "192.168.68.50", 2023, "192.168.68.12", 51002, EST, 0, 0);
    fx_write(&f);
    assert(setenv(CP0_SSH_PORTS_ENV, "2022,2023", 1) == 0);
    assert(count_after_two(f.root, &s) == 1 && s.listening == 1); /* 2023 has no listener */
    unsetenv(CP0_SSH_PORTS_ENV);

    /* $APPLAUNCH_STATUS_SYSFS_ROOT is the default root; under a root the activity source is the queue fallback */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    assert(setenv(CP0_SSH_ROOT_ENV, f.root, 1) == 0);
    cp0_ssh_watch_init(&w, NULL);
    cp0_ssh_watch_sample(&w, 0, &s);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.source == CP0_SSH_ACT_QUEUE);
    unsetenv(CP0_SSH_ROOT_ENV);

    /* the tracking cap: 40 sessions are seen as CP0_SSH_MAX_CONNS */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    for (int i = 0; i < 40; ++i) add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 50000 + i, EST, 0, 0);
    fx_write(&f);
    assert(count_after_two(f.root, NULL) == CP0_SSH_MAX_CONNS);

    /* NULL arguments */
    assert(cp0_ssh_watch_sample(NULL, 0, &s) == 0 && s.count == 0);
    assert(cp0_ssh_watch_sample(&w, 0, NULL) == 0);
    cp0_ssh_watch_init(NULL, NULL);
    cp0_ssh_watch_reload(NULL);
}

/* --------------------------------------------------------------------------------------------------- confirmation */

static void test_confirmation(void)
{
    fixture_t f;
    cp0_ssh_status_t s;
    cp0_ssh_watch_t w;

    /* seen once (a connect scan, a handshake-only attempt): never counted */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.66", 40000, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, NULL);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0);
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 0);

    /* a missed sample starts over */
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 6000, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 8000, &s) == 1);
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 10000, &s) == 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 12000, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 14000, &s) == 1);

    /* a fast caller: two samples 500 ms apart are not enough, the confirmation waits for CP0_SSH_CONFIRM_MS */
    watch_at(&w, f.root, NULL);
    assert(cp0_ssh_watch_sample(&w, 100000, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 100500, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 100999, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 100000 + CP0_SSH_CONFIRM_MS, &s) == 1);

    /* the config is re-read after the period, or at once after cp0_ssh_watch_reload() */
    watch_at(&w, f.root, NULL);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1);
    put(f.root, "etc/ssh/sshd_config", "Port 2222\n");
    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 1); /* within the period: the old port set */
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_CONFIG_PERIOD_MS, &s) == 0 && s.listening == 0);
    put(f.root, "etc/ssh/sshd_config", "Port 22\n");
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_CONFIG_PERIOD_MS + 2000, &s) == 0);
    cp0_ssh_watch_reload(&w);
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_CONFIG_PERIOD_MS + 4000, &s) == 0 && s.listening == 1); /* seen again */
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_CONFIG_PERIOD_MS + 6000, &s) == 1);
}

/* ------------------------------------------------------------------------------------------------------- activity */

static void test_activity_bytes(void)
{
    fixture_t f;
    cp0_ssh_status_t s;
    cp0_ssh_watch_t w;
    fake_counters_t fake;
    memset(&fake, 0, sizeof(fake));

    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, &fake); /* the hook is used under a root too */
    fake_set(&fake, 51000, 1000);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0 && s.active == 0 && s.source == CP0_SSH_ACT_BYTES);
    fake_set(&fake, 51000, 5000); /* the login moved bytes: active as soon as it counts */
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.active == 1 && s.source == CP0_SSH_ACT_BYTES);
    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 1 && s.active == 0);
    fake_set(&fake, 51000, 5000 + CP0_SSH_ACTIVE_MIN_BYTES);
    assert(cp0_ssh_watch_sample(&w, 6000, &s) == 1 && s.active == 1);
    assert(cp0_ssh_watch_sample(&w, 8000, &s) == 1 && s.active == 0);
    assert(fake.calls == 5);

    /* a second, newer session moving bytes does not make the first one active until it counts itself */
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51001, EST, 0, 0);
    fx_write(&f);
    fake_set(&fake, 51001, 100);
    assert(cp0_ssh_watch_sample(&w, 10000, &s) == 1 && s.active == 0);
    fake_set(&fake, 51001, 900);
    assert(cp0_ssh_watch_sample(&w, 12000, &s) == 2 && s.active == 1);
    assert(cp0_ssh_watch_sample(&w, 14000, &s) == 2 && s.active == 0);

    /* gone: nothing to measure, the hook is not called */
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    fx_write(&f);
    const int calls = fake.calls;
    assert(cp0_ssh_watch_sample(&w, 16000, &s) == 0 && s.active == 0 && s.source == CP0_SSH_ACT_NONE);
    assert(fake.calls == calls);

    /* a connection the counters do not find at first: no baseline, no activity until two readings */
    memset(&fake, 0, sizeof(fake));
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51002, EST, 0, 0);
    fx_write(&f);
    watch_at(&w, f.root, &fake);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0);
    fake_set(&fake, 51002, 7000);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.active == 0);
    fake_set(&fake, 51002, 7100);
    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 1 && s.active == 1);
}

static void test_activity_queue(void)
{
    fixture_t f;
    cp0_ssh_status_t s;
    cp0_ssh_watch_t w;
    fake_counters_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.fail = 1;

    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0x200);
    fx_write(&f);
    watch_at(&w, f.root, &fake);
    /* the counters fail: queue fallback; a queue on an unconfirmed connection is not activity */
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0 && s.active == 0 && s.source == CP0_SSH_ACT_QUEUE);
    assert(fake.calls == 1);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.active == 1 && s.source == CP0_SSH_ACT_QUEUE);
    assert(fake.calls == 1); /* waiting CP0_SSH_DIAG_RETRY_MS */
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 1 && s.active == 0);
    fx_clear(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    add4(&f.t4, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0x1000, 0);
    fx_write(&f);
    assert(cp0_ssh_watch_sample(&w, 6000, &s) == 1 && s.active == 1);

    /* after the retry delay the counters are asked again; they work now: exact, with a fresh baseline */
    fake.fail = 0;
    fake_set(&fake, 51000, 300);
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_DIAG_RETRY_MS - 1, &s) == 1 && s.source == CP0_SSH_ACT_QUEUE);
    assert(fake.calls == 1);
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_DIAG_RETRY_MS, &s) == 1 && s.source == CP0_SSH_ACT_BYTES);
    assert(fake.calls == 2 && s.active == 0); /* the queue says busy, the counters have no baseline yet */
    fake_set(&fake, 51000, 400);
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_DIAG_RETRY_MS + 2000, &s) == 1 && s.active == 1);
    assert(cp0_ssh_watch_sample(&w, CP0_SSH_DIAG_RETRY_MS + 4000, &s) == 1 && s.active == 0);

    /* no hook under a root: the queue fallback from the start */
    watch_at(&w, f.root, NULL);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0 && s.source == CP0_SSH_ACT_QUEUE);
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 1 && s.active == 1);
}

/* ------------------------------------------------------------------------------------------------ malformed files */

static void test_malformed_files(void)
{
    fixture_t f;
    cp0_ssh_status_t s;
    text_t good;

    /* junk lines around a good one, CRLF, an over-long line before a good one, a truncated last line */
    fx_init(&f);
    t_init(&good, 4);
    good.len = 0;
    add4(&good, "192.168.68.50", 22, "192.168.68.12", 51000, EST, 0, 0);
    add4(&good, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    static char text[24000];
    size_t at = 0;
    at += (size_t)snprintf(text + at, sizeof(text) - at, "%s", "garbage\nhello world 1 2 3 4 5 6 7 8 9 10\n\n\n:::\n");
    memset(text + at, 'x', 5000); /* longer than the reader's buffer */
    at += 5000;
    text[at++] = '\n';
    const size_t first_len = (size_t)(strchr(good.text, '\n') - good.text);
    memcpy(text + at, good.text, first_len);
    at += first_len;
    text[at++] = '\r'; /* CRLF */
    text[at++] = '\n';
    at += (size_t)snprintf(text + at, sizeof(text) - at, "%s", strchr(good.text, '\n') + 1);
    at += (size_t)snprintf(text + at, sizeof(text) - at, "%s", "   9: 0100007F:0016 0100"); /* truncated, no '\n' */
    put_bytes(f.root, "proc/net/tcp", text, at);
    /* tcp6: binary bytes and NULs */
    char junk[600];
    for (size_t i = 0; i < sizeof(junk); ++i) junk[i] = (char)(i * 37 + 11);
    junk[17] = '\0';
    junk[300] = '\n';
    put_bytes(f.root, "proc/net/tcp6", junk, sizeof(junk));
    assert(count_after_two(f.root, NULL) == 1);

    /* an over-long last line without its line end, empty tcp, missing tcp6 */
    fx_init(&f);
    memset(text, 'y', 9000);
    put_bytes(f.root, "proc/net/tcp6", text, 9000);
    put(f.root, "proc/net/tcp", "");
    assert(count_after_two(f.root, &s) == 0 && s.listening == 0);
    fx_init(&f);
    put(f.root, "proc/net/tcp", "  sl  local_address rem_address\n");
    assert(count_after_two(f.root, NULL) == 0);
    fx_init(&f); /* no /proc at all */
    assert(count_after_two(f.root, NULL) == 0);

    /* a truncated line that still has every field (cut inside the inode) parses: the inode is not used to match */
    fx_init(&f);
    add4(&f.t4, "0.0.0.0", 22, "0.0.0.0", 0, LST, 0, 0);
    tput(&f.t4, "   1: %08X:0016 %08X:C738 01 00000000:00000000 00:00000000 00000000  0 0 12", (unsigned)v4("10.0.0.2"),
         (unsigned)v4("10.0.0.9"));
    fx_write(&f);
    assert(count_after_two(f.root, NULL) == 1);
}

/* ---------------------------------------------------------------------------------------------- real loopback */

static void pause_ms(int ms)
{
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static void send_all(int fd, size_t n)
{
    char chunk[1024];
    memset(chunk, 'z', sizeof(chunk));
    while (n > 0) {
        const size_t part = n < sizeof(chunk) ? n : sizeof(chunk);
        const ssize_t sent = send(fd, chunk, part, 0);
        assert(sent > 0);
        n -= (size_t)sent;
    }
}

static void recv_all(int fd, size_t n)
{
    char chunk[1024];
    while (n > 0) {
        const ssize_t got = recv(fd, chunk, n < sizeof(chunk) ? n : sizeof(chunk), 0);
        assert(got > 0);
        n -= (size_t)got;
    }
}

/* bytes both ways, then time for the last ACKs (a delayed ACK must not land after the next sample) */
static void exchange(int client, int server)
{
    send_all(client, 3000);
    recv_all(server, 3000);
    send_all(server, 2000);
    recv_all(client, 2000);
    pause_ms(300);
}

/* family AF_INET: a 127.0.0.1 listener; AF_INET6: a dual-stack [::] listener, the IPv4 client is v4-mapped in tcp6 */
static int loopback_case(int family)
{
    const char *name = family == AF_INET ? "IPv4" : "IPv6 dual-stack (v4-mapped)";
    const int listener = socket(family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener < 0) {
        printf("SKIP ssh watch loopback %s: no socket (%s)\n", name, strerror(errno));
        return 0;
    }
    int bound;
    if (family == AF_INET) {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bound = bind(listener, (struct sockaddr *)&a, sizeof(a));
    } else {
        int off = 0;
        setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
        struct sockaddr_in6 a;
        memset(&a, 0, sizeof(a));
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_any;
        bound = bind(listener, (struct sockaddr *)&a, sizeof(a));
    }
    if (bound != 0 || listen(listener, 4) != 0) {
        printf("SKIP ssh watch loopback %s: cannot listen (%s)\n", name, strerror(errno));
        close(listener);
        return 0;
    }
    struct sockaddr_storage where;
    socklen_t where_len = sizeof(where);
    assert(getsockname(listener, (struct sockaddr *)&where, &where_len) == 0);
    const uint16_t port = ntohs(family == AF_INET ? ((struct sockaddr_in *)&where)->sin_port
                                                  : ((struct sockaddr_in6 *)&where)->sin6_port);
    char value[16];
    snprintf(value, sizeof(value), "%u", (unsigned)port);
    assert(setenv(CP0_SSH_PORTS_ENV, value, 1) == 0);

    cp0_ssh_watch_t w;
    cp0_ssh_status_t s;
    cp0_ssh_peer_t peers[4];
    cp0_ssh_watch_options_t options = {"", NULL, NULL}; /* the real /proc whatever the environment says */
    cp0_ssh_watch_init(&w, &options);
    assert(cp0_ssh_watch_sample(&w, 0, &s) == 0 && s.listening == 1 && s.source == CP0_SSH_ACT_NONE);

    const int client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(client >= 0);
    /* from 127.0.0.2 (all of 127/8 is loopback on Linux), so the two ends' addresses differ, as from a real peer */
    const char *peer = "127.0.0.1";
    struct sockaddr_in from;
    memset(&from, 0, sizeof(from));
    from.sin_family = AF_INET;
    from.sin_addr.s_addr = htonl(INADDR_LOOPBACK + 1);
    if (bind(client, (struct sockaddr *)&from, sizeof(from)) == 0) peer = "127.0.0.2";
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(port);
    assert(connect(client, (struct sockaddr *)&to, sizeof(to)) == 0);
    const int server = accept(listener, NULL, NULL);
    assert(server >= 0);
    struct sockaddr_in me;
    socklen_t me_len = sizeof(me);
    assert(getsockname(client, (struct sockaddr *)&me, &me_len) == 0);

    /* can this kernel answer sock_diag for this user? */
    cp0_ssh_conn_t probe[CP0_SSH_MAX_CONNS];
    int nprobe = 0;
    errno = 0;
    assert(cp0_ssh_watch_sample(&w, 2000, &s) == 0); /* first sighting */
    const int netlink = s.source == CP0_SSH_ACT_BYTES;
    if (!netlink) {
        memcpy(probe, w.conns, sizeof(probe[0]) * (size_t)w.nconns);
        nprobe = w.nconns;
        const int rc = cp0_ssh_diag_counters(NULL, probe, nprobe);
        const char *why = rc == 0 ? "no byte counters" : strerror(errno);
        if (getenv("REQUIRE_SOCK_DIAG") && strcmp(getenv("REQUIRE_SOCK_DIAG"), "1") == 0) {
            fprintf(stderr, "FAIL ssh watch loopback %s: NETLINK_SOCK_DIAG unavailable (%s)\n", name, why);
            assert(0);
        }
        printf("SKIP ssh watch loopback %s activity: NETLINK_SOCK_DIAG unavailable here (%s); count checks only\n",
               name, why);
    }

    assert(cp0_ssh_watch_sample(&w, 4000, &s) == 1);
    assert(cp0_ssh_watch_peers(&w, peers, 4) == 1);
    assert(strcmp(peers[0].addr, peer) == 0 && peers[0].port == ntohs(me.sin_port));

    exchange(client, server);
    assert(cp0_ssh_watch_sample(&w, 6000, &s) == 1); /* the client end (ephemeral local port) never counts */
    if (netlink) assert(s.source == CP0_SSH_ACT_BYTES && s.active == 1);
    assert(cp0_ssh_watch_sample(&w, 8000, &s) == 1);
    if (netlink) assert(s.active == 0);
    send_all(client, 48); /* a keystroke, no echo: only the server's bytes_received moves */
    recv_all(server, 48);
    pause_ms(300);
    assert(cp0_ssh_watch_sample(&w, 10000, &s) == 1);
    if (netlink) assert(s.active == 1);
    assert(cp0_ssh_watch_sample(&w, 12000, &s) == 1);
    if (netlink) assert(s.active == 0);
    send_all(server, 64); /* output only: only the server's bytes_acked moves */
    recv_all(client, 64);
    pause_ms(300);
    assert(cp0_ssh_watch_sample(&w, 14000, &s) == 1);
    if (netlink) assert(s.active == 1);
    assert(cp0_ssh_watch_sample(&w, 16000, &s) == 1);
    if (netlink) assert(s.active == 0);

    close(server);
    close(client);
    assert(cp0_ssh_watch_sample(&w, 18000, &s) == 0 && s.active == 0);
    close(listener);
    assert(cp0_ssh_watch_sample(&w, 20000, &s) == 0 && s.listening == 0);
    unsetenv(CP0_SSH_PORTS_ENV);
    printf("ssh watch loopback %s: count 0 -> 1 -> 0%s\n", name,
           netlink ? ", activity (both ways, in only, out only) on / off through NETLINK_SOCK_DIAG" : "");
    return 1;
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_base, sizeof(g_base), "%s/cp0-ssh-watch-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    assert(mkdtemp(g_base) != NULL);
    unsetenv(CP0_SSH_PORTS_ENV);
    unsetenv(CP0_SSH_ROOT_ENV);

    test_parse_line();
    test_config_ports();
    test_detection();
    test_confirmation();
    test_activity_bytes();
    test_activity_queue();
    test_malformed_files();
    loopback_case(AF_INET);
    loopback_case(AF_INET6);

    char command[400];
    snprintf(command, sizeof(command), "rm -rf '%s'", g_base);
    if (system(command) != 0) fprintf(stderr, "could not remove %s\n", g_base);
    printf("ssh watch OK\n");
    return 0;
}
