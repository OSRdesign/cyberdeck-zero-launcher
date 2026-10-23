/*
 * The status feed (report 032): status.state written by the launcher (cp0_status_state.c) and read back by the app
 * contract's reader (cp0_app_status_*): path, exact text, format / parse round trip, atomic write, a missing file, a
 * wrong version, unknown keys, and a launcher that is gone (stale pid) or alive (own pid, a pid of another user).
 */
#define _GNU_SOURCE

#include "cp0_status_state.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static cp0_app_status_t sample(void)
{
    cp0_app_status_t s;
    memset(&s, 0, sizeof(s));
    s.pid = (int)getpid();
    s.wifi = 1;
    s.wifi_pct = 72;
    s.eth = 1;
    s.bt = CP0_APP_BT_CONNECTED;
    s.battery = 64;
    snprintf(s.battery_state, sizeof(s.battery_state), "Not charging");
    s.gen = 7;
    return s;
}

static void test_path(void)
{
    char buf[256];
    CHECK(cp0_status_state_path("/run/user/1000", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/status.state") == 0);
    CHECK(cp0_status_state_path(NULL, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-status.state") == 0);
    CHECK(cp0_status_state_path("", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-status.state") == 0);
    char small[10];
    CHECK(cp0_status_state_path("/run/user/1000", small, sizeof(small)) == -1);
    /* the launcher's place is the apps' place */
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    unsetenv("APPLAUNCH_STATE_DIR");
    char app[256];
    CHECK(cp0_app_state_path(CP0_APP_STATUS_FILE, app, sizeof(app)) == 0);
    CHECK(strcmp(app, "/run/user/1000/applaunch/status.state") == 0);
    unsetenv("XDG_RUNTIME_DIR");
    CHECK(cp0_app_state_path(CP0_APP_STATUS_FILE, app, sizeof(app)) == 0);
    CHECK(strcmp(app, "/tmp/applaunch-status.state") == 0);
}

static void test_format_parse(void)
{
    char text[256];
    cp0_app_status_t s = sample();
    const int n = cp0_status_state_format(&s, text, sizeof(text));
    CHECK(n > 0 && (size_t)n == strlen(text));
    char want[256];
    snprintf(want, sizeof(want),
             "v=1\ngen=7\npid=%d\nwifi=1\nwifi_pct=72\neth=1\nbt=connected\nbattery=64\nbattery_state=Not charging\n",
             (int)getpid());
    CHECK(strcmp(text, want) == 0);
    char tiny[20];
    CHECK(cp0_status_state_format(&s, tiny, sizeof(tiny)) == -1);

    cp0_app_status_t back;
    CHECK(cp0_app_status_parse(text, (size_t)n, &back) == 0);
    CHECK(back.pid == s.pid && back.wifi == 1 && back.wifi_pct == 72 && back.eth == 1);
    CHECK(back.bt == CP0_APP_BT_CONNECTED && back.battery == 64 && back.gen == 7);
    CHECK(strcmp(back.battery_state, "Not charging") == 0 && back.battery_charging == 0);

    /* every bluetooth word, charging, no battery */
    s.bt = CP0_APP_BT_ON;
    snprintf(s.battery_state, sizeof(s.battery_state), "Charging");
    CHECK(cp0_status_state_format(&s, text, sizeof(text)) > 0 && strstr(text, "bt=on\n"));
    CHECK(cp0_app_status_parse(text, strlen(text), &back) == 0 && back.bt == CP0_APP_BT_ON && back.battery_charging == 1);
    s.bt = CP0_APP_BT_OFF;
    s.battery = -1;
    s.battery_state[0] = '\0';
    CHECK(cp0_status_state_format(&s, text, sizeof(text)) > 0 && strstr(text, "bt=off\n") && strstr(text, "battery=-1\n"));
    CHECK(cp0_app_status_parse(text, strlen(text), &back) == 0 && back.bt == CP0_APP_BT_OFF && back.battery == -1);

    /* tolerant reader: CRLF, unknown keys, out of range values keep the default */
    const char *odd = "v=1\r\nfuture=1\r\nwifi=1\r\nwifi_pct=400\r\nbattery=300\r\nbt=sleeping\r\npid=5\r\n";
    CHECK(cp0_app_status_parse(odd, strlen(odd), &back) == 0);
    CHECK(back.wifi == 1 && back.wifi_pct == 0 && back.battery == -1 && back.bt == CP0_APP_BT_OFF && back.pid == 5);

    /* not a v=1 file: refused, out cleared (battery -1) */
    CHECK(cp0_app_status_parse("v=2\nwifi=1\n", 11, &back) == -1 && back.wifi == 0 && back.battery == -1);
    CHECK(cp0_app_status_parse("wifi=1\n", 7, &back) == -1);
    CHECK(cp0_app_status_parse(NULL, 0, &back) == -1);
}

static int count_entries(const char *dir)
{
    int n = 0;
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)))
        if (e->d_name[0] != '.') n++;
    if (d) closedir(d);
    return n;
}

static void test_write_read(void)
{
    char root[] = "/tmp/cp0-status-state-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512], dir[512];
    CHECK(cp0_status_state_path(root, path, sizeof(path)) == 0);
    snprintf(dir, sizeof(dir), "%s/applaunch", root);

    const mode_t old_umask = umask(077);
    cp0_app_status_t s = sample();
    CHECK(cp0_status_state_write(path, &s) == 0); /* creates <runtime>/applaunch */
    umask(old_umask);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644);

    /* an app reads it through APPLAUNCH_STATE_DIR, by path, and the live pid (ours) is trusted */
    cp0_app_status_t back;
    CHECK(cp0_app_status_read(path, &back) == 0 && back.wifi_pct == 72 && back.battery == 64 && back.gen == 7);
    setenv("APPLAUNCH_STATE_DIR", dir, 1);
    CHECK(cp0_app_status_read(NULL, &back) == 0 && back.bt == CP0_APP_BT_CONNECTED);

    /* replaced, never appended, no temporary left */
    s.gen = 8;
    s.wifi = 0;
    CHECK(cp0_status_state_write(path, &s) == 0);
    CHECK(cp0_app_status_read(NULL, &back) == 0 && back.gen == 8 && back.wifi == 0);
    CHECK(count_entries(dir) == 1);

    /* a pid of another user (init) is alive for kill(pid, 0): EPERM counts as alive */
    s.pid = 1;
    CHECK(cp0_status_state_write(path, &s) == 0 && cp0_app_status_read(NULL, &back) == 0);

    /* the launcher is gone: a child that has exited and been reaped */
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) _exit(0);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    s.pid = (int)child;
    CHECK(cp0_status_state_write(path, &s) == 0);
    CHECK(cp0_app_status_read(NULL, &back) == -1 && back.wifi == 0 && back.battery == -1 && back.pid == 0);
    CHECK(cp0_app_status_load(NULL, &back) == 0 && back.pid == (int)child); /* the raw load still parses it */

    /* no pid at all: not trusted */
    s.pid = 0;
    CHECK(cp0_status_state_write(path, &s) == 0 && cp0_app_status_read(NULL, &back) == -1);

    /* missing file, unwritable place */
    CHECK(cp0_app_status_read("/nonexistent/status.state", &back) == -1 && back.battery == -1);
    CHECK(cp0_status_state_write("/proc/applaunch-test/status.state", &s) == -1);
    unsetenv("APPLAUNCH_STATE_DIR");

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

int main(void)
{
    test_path();
    test_format_parse();
    test_write_read();
    printf("test_status_state: PASS\n");
    return 0;
}
