/*
 * SPDX-License-Identifier: MIT
 *
 * Multitasking P-e: the app side of the lifecycle protocol (cp0_app_lifecycle.h) over a real SOCK_SEQPACKET socketpair,
 * the test playing the launcher: parsing (the launcher's own rules, lifecycle_protocol.cpp), the environment, the
 * hide / show state machine (the op runs before the answer, a hide while hidden and a show while visible only answer,
 * messages handled in order, unknown ones ignored without an answer), the answer time, close, the launcher going away
 * while visible and while hidden, "home" / "switcher", and the key rule (hidden, Alt+Tab, orphan releases after show).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cp0_app_lifecycle.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int g_fail, g_pass;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (cond) g_pass++;                                                                                            \
        else {                                                                                                         \
            g_fail++;                                                                                                  \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
        }                                                                                                              \
    } while (0)

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* ---------------------------------------------------------------- the app's ops, recording */

typedef struct {
    int hides, shows, closes;
    uint32_t last_n;
    int launcher_fd;   /* the other end: an answer must not be there yet when an op runs */
    int answer_early;  /* an op saw an answer already waiting */
} rec_t;

static int readable(int fd)
{
    struct pollfd p = {fd, POLLIN, 0};
    return poll(&p, 1, 0) > 0 && (p.revents & POLLIN);
}

static void op_hide(void *ctx, uint32_t n)
{
    rec_t *r = ctx;
    r->hides++;
    r->last_n = n;
    if (readable(r->launcher_fd)) r->answer_early = 1;
}

static void op_show(void *ctx, uint32_t n)
{
    rec_t *r = ctx;
    r->shows++;
    r->last_n = n;
    if (readable(r->launcher_fd)) r->answer_early = 1;
}

static void op_close(void *ctx, uint32_t n)
{
    rec_t *r = ctx;
    r->closes++;
    r->last_n = n;
}

static const cp0_app_lifecycle_ops_t ops = {op_hide, op_show, op_close};

static void say(int fd, const char *msg)
{
    CHECK(send(fd, msg, strlen(msg), MSG_NOSIGNAL) == (ssize_t)strlen(msg));
}

/* the next datagram the app sent ("" when none) */
static const char *heard(int fd)
{
    static char buf[128];
    const ssize_t r = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    buf[r > 0 ? r : 0] = '\0';
    return buf;
}

static void pair(int sv[2])
{
    CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);
}

/* ---------------------------------------------------------------- tests */

static void test_parse(void)
{
    uint32_t n = 7;
    CHECK(cp0_app_lifecycle_parse("hide 3", 6, &n) == CP0_APP_LC_HIDE && n == 3);
    CHECK(cp0_app_lifecycle_parse("show 4\n", 7, &n) == CP0_APP_LC_SHOW && n == 4);
    CHECK(cp0_app_lifecycle_parse("close 5", 8, &n) == CP0_APP_LC_CLOSE && n == 5); /* the NUL counted */
    CHECK(cp0_app_lifecycle_parse("hide 4294967295", 15, &n) == CP0_APP_LC_HIDE && n == 4294967295u);
    CHECK(cp0_app_lifecycle_parse("hide 0", 6, &n) == CP0_APP_LC_HIDE && n == 0);
    CHECK(cp0_app_lifecycle_parse("hide 4294967296", 15, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("hide", 4, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("hide ", 5, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("hide x", 6, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("hide 3 4", 8, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("hide -3", 7, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse(" hide 3", 7, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("HIDE 3", 6, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("ack 3", 5, &n) == CP0_APP_LC_NONE);  /* the app's own words, not the launcher's */
    CHECK(cp0_app_lifecycle_parse("home", 4, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("switcher", 8, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse("", 0, &n) == CP0_APP_LC_NONE);
    CHECK(cp0_app_lifecycle_parse(NULL, 4, &n) == CP0_APP_LC_NONE);
    char longer[80];
    memset(longer, '1', sizeof(longer));
    memcpy(longer, "hide ", 5);
    CHECK(cp0_app_lifecycle_parse(longer, 65, &n) == CP0_APP_LC_NONE); /* over 64 bytes */
    CHECK(cp0_app_lifecycle_parse("hide 12\n\n", 9, NULL) == CP0_APP_LC_HIDE);
}

static void test_env(void)
{
    cp0_app_lifecycle_t lc;
    unsetenv(CP0_APP_LIFECYCLE_ENV);
    unsetenv(CP0_APP_LIFECYCLE_FD_ENV);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0 && lc.fd == -1);
    CHECK(cp0_app_lifecycle_service(&lc) == 0); /* without a lifecycle: nothing, no error */
    CHECK(cp0_app_lifecycle_send(&lc, "home") == -1);

    int sv[2];
    pair(sv);
    char num[16];
    snprintf(num, sizeof(num), "%d", sv[1]);
    setenv(CP0_APP_LIFECYCLE_FD_ENV, num, 1);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0); /* the fd alone means nothing */
    setenv(CP0_APP_LIFECYCLE_ENV, "0", 1);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0);
    setenv(CP0_APP_LIFECYCLE_ENV, "1", 1);
    setenv(CP0_APP_LIFECYCLE_FD_ENV, "x", 1);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0);
    setenv(CP0_APP_LIFECYCLE_FD_ENV, "999", 1); /* not open */
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0);
    const int devnull = open("/dev/null", O_RDONLY);
    snprintf(num, sizeof(num), "%d", devnull);
    setenv(CP0_APP_LIFECYCLE_FD_ENV, num, 1);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 0); /* open, but not a socket */
    close(devnull);
    snprintf(num, sizeof(num), "%d", sv[1]);
    setenv(CP0_APP_LIFECYCLE_FD_ENV, num, 1);
    CHECK(cp0_app_lifecycle_init(&lc, &ops, NULL) == 1 && lc.fd == sv[1]);
    CHECK((fcntl(sv[1], F_GETFL) & O_NONBLOCK) != 0);
    CHECK((fcntl(sv[1], F_GETFD) & FD_CLOEXEC) != 0); /* helpers the app starts do not inherit it */
    CHECK(cp0_app_lifecycle_service(&lc) == 0);         /* nothing waiting: returns at once */
    cp0_app_lifecycle_close(&lc);
    CHECK(lc.fd == -1 && fcntl(sv[1], F_GETFD) < 0);
    close(sv[0]);
    unsetenv(CP0_APP_LIFECYCLE_ENV);
    unsetenv(CP0_APP_LIFECYCLE_FD_ENV);
}

static void test_cycle(void)
{
    int sv[2];
    pair(sv);
    rec_t r;
    memset(&r, 0, sizeof(r));
    r.launcher_fd = sv[0];
    cp0_app_lifecycle_t lc;
    CHECK(cp0_app_lifecycle_attach(&lc, sv[1], &ops, &r) == 1);

    say(sv[0], "hide 1");
    const double t0 = now_ms();
    CHECK(cp0_app_lifecycle_service(&lc) == 1);
    const double took = now_ms() - t0;
    CHECK(strcmp(heard(sv[0]), "ack 1") == 0);
    CHECK(took < 50.0); /* the deadline is 300 ms; here it is the time of one recv, the op and one send */
    CHECK(r.hides == 1 && r.last_n == 1 && lc.hidden == 1 && lc.hides == 1 && lc.acks == 1);
    CHECK(r.answer_early == 0); /* the op ran before the answer */

    say(sv[0], "hide 2"); /* already hidden: answered, the op not run again */
    CHECK(cp0_app_lifecycle_service(&lc) == 1);
    CHECK(strcmp(heard(sv[0]), "ack 2") == 0 && r.hides == 1 && lc.hidden == 1);

    say(sv[0], "show 3");
    CHECK(cp0_app_lifecycle_service(&lc) == 1);
    CHECK(strcmp(heard(sv[0]), "ack 3") == 0);
    CHECK(r.shows == 1 && r.last_n == 3 && lc.hidden == 0 && r.answer_early == 0);

    say(sv[0], "show 4\n"); /* visible: answered only */
    CHECK(cp0_app_lifecycle_service(&lc) == 1);
    CHECK(strcmp(heard(sv[0]), "ack 4") == 0 && r.shows == 1);

    /* unknown messages: ignored, no answer */
    say(sv[0], "bogus 5");
    say(sv[0], "ack 6");
    say(sv[0], "home");
    char big[100];
    memset(big, '9', sizeof(big));
    memcpy(big, "hide ", 5);
    CHECK(send(sv[0], big, sizeof(big), MSG_NOSIGNAL) == (ssize_t)sizeof(big));
    CHECK(cp0_app_lifecycle_service(&lc) == 4);
    CHECK(heard(sv[0])[0] == '\0' && r.hides == 1 && lc.hidden == 0);

    /* several waiting: handled in order in one call (a slow app answering a hide late, then the show) */
    say(sv[0], "hide 7");
    say(sv[0], "show 8");
    CHECK(cp0_app_lifecycle_service(&lc) == 2);
    CHECK(strcmp(heard(sv[0]), "ack 7") == 0);
    CHECK(strcmp(heard(sv[0]), "ack 8") == 0);
    CHECK(r.hides == 2 && r.shows == 2 && lc.hidden == 0 && lc.acks == 6);

    /* the app's own messages */
    CHECK(cp0_app_lifecycle_send(&lc, "home") == 0);
    CHECK(strcmp(heard(sv[0]), "home") == 0);
    CHECK(cp0_app_lifecycle_send(&lc, "switcher") == 0);
    CHECK(strcmp(heard(sv[0]), "switcher") == 0);
    CHECK(cp0_app_lifecycle_send(&lc, "") == -1);
    big[sizeof(big) - 1] = '\0';
    CHECK(cp0_app_lifecycle_send(&lc, big) == -1); /* over 64 bytes */

    /* close: the op, no answer, nothing handled after it */
    say(sv[0], "close 9");
    say(sv[0], "show 10");
    CHECK(cp0_app_lifecycle_service(&lc) == 1);
    CHECK(r.closes == 1 && r.last_n == 9 && lc.closing == 1);
    CHECK(heard(sv[0])[0] == '\0');
    CHECK(cp0_app_lifecycle_service(&lc) == 0 && r.shows == 2); /* closing: the rest is not read */
    cp0_app_lifecycle_close(&lc);
    close(sv[0]);
}

static void test_hang_up(void)
{
    /* visible: the lifecycle ends, the app goes on */
    int sv[2];
    pair(sv);
    rec_t r;
    memset(&r, 0, sizeof(r));
    r.launcher_fd = sv[0];
    cp0_app_lifecycle_t lc;
    cp0_app_lifecycle_attach(&lc, sv[1], &ops, &r);
    close(sv[0]);
    CHECK(cp0_app_lifecycle_service(&lc) == -1);
    CHECK(lc.fd == -1 && r.closes == 0 && lc.closing == 0);
    CHECK(cp0_app_lifecycle_service(&lc) == 0);
    CHECK(cp0_app_lifecycle_send(&lc, "home") == -1);

    /* hidden: nobody can show it again, so it closes */
    pair(sv);
    memset(&r, 0, sizeof(r));
    r.launcher_fd = sv[0];
    cp0_app_lifecycle_attach(&lc, sv[1], &ops, &r);
    say(sv[0], "hide 1");
    CHECK(cp0_app_lifecycle_service(&lc) == 1 && lc.hidden == 1);
    CHECK(strcmp(heard(sv[0]), "ack 1") == 0);
    close(sv[0]);
    CHECK(cp0_app_lifecycle_service(&lc) == -1);
    CHECK(lc.fd == -1 && r.closes == 1 && lc.closing == 1);
}

static void test_keys(void)
{
    enum { KEY_ESC = 1, KEY_LEFTALT = 56, KEY_ENTER = 28 };
    cp0_app_lc_keys_t k;
    memset(&k, 0, sizeof(k));
    /* before any show: everything passes but Alt+Tab */
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ENTER, CP0_APP_LC_KEY_RELEASED, 0) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_LEFTALT, CP0_APP_LC_KEY_PRESSED, 1) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, CP0_APP_LC_KEY_TAB, CP0_APP_LC_KEY_PRESSED, 1) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, CP0_APP_LC_KEY_TAB, CP0_APP_LC_KEY_REPEATED, 1) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, CP0_APP_LC_KEY_TAB, CP0_APP_LC_KEY_RELEASED, 1) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, CP0_APP_LC_KEY_TAB, CP0_APP_LC_KEY_PRESSED, 0) == 0); /* plain Tab */
    /* hidden: everything dropped */
    CHECK(cp0_app_lifecycle_key_drop(&k, 1, KEY_ESC, CP0_APP_LC_KEY_PRESSED, 0) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 1, 0, CP0_APP_LC_KEY_RELEASED, 0) == 1);
    /* after a show: the release of Alt (held when Alt+Tab hid the app, or made up when the keyboards were let go) is
     * an orphan, as is a repeat of a key held meanwhile; a key pressed after the show goes through, press and release */
    cp0_app_lifecycle_keys_shown(&k);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_LEFTALT, CP0_APP_LC_KEY_RELEASED, 0) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ESC, CP0_APP_LC_KEY_REPEATED, 0) == 1);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ENTER, CP0_APP_LC_KEY_PRESSED, 0) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ENTER, CP0_APP_LC_KEY_REPEATED, 0) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ENTER, CP0_APP_LC_KEY_RELEASED, 0) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ENTER, CP0_APP_LC_KEY_RELEASED, 0) == 1); /* released twice */
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, 0, CP0_APP_LC_KEY_RELEASED, 0) == 0);         /* text without a key */
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, 900, CP0_APP_LC_KEY_RELEASED, 0) == 0);       /* past KEY_CNT: kept */
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, 767, CP0_APP_LC_KEY_PRESSED, 0) == 0);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, 767, CP0_APP_LC_KEY_RELEASED, 0) == 0);
    /* a second show forgets the keys down */
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ESC, CP0_APP_LC_KEY_PRESSED, 0) == 0);
    cp0_app_lifecycle_keys_shown(&k);
    CHECK(cp0_app_lifecycle_key_drop(&k, 0, KEY_ESC, CP0_APP_LC_KEY_RELEASED, 0) == 1);
}

int main(void)
{
    test_parse();
    test_env();
    test_cycle();
    test_hang_up();
    test_keys();
    printf("test_app_lifecycle: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
