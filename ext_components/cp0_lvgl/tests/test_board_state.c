/*
 * board.state (report 035): the exact text, the round trip through cp0_app_board_parse(), absent keys stay -1 / "", a
 * tolerant reader, the atomic write and the launcher-is-gone rule, the requests= list.
 */
#define _GNU_SOURCE

#include "cp0_board_state.h"

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

static cp0_app_board_t sample(void)
{
    cp0_app_board_t b;
    memset(&b, 0, sizeof(b));
    b.pid = (int)getpid();
    b.gen = 3;
    snprintf(b.launcher, sizeof(b.launcher), "0.8.6");
    snprintf(b.board, sizeof(b.board), "hackberry-cm5");
    snprintf(b.arch, sizeof(b.arch), "aarch64");
    b.ram_mb = 1800;
    b.battery = 1;
    snprintf(b.battery_source, sizeof(b.battery_source), "gauge:max17048");
    snprintf(b.backlight, sizeof(b.backlight), "levels");
    b.backlight_steps = 10;
    b.sound = 1;
    snprintf(b.sound_kind, sizeof(b.sound_kind), "bluetooth");
    b.sound_guess = 1;
    snprintf(b.requests, sizeof(b.requests), "hello,caps,toast,beep,volume,brightness,dim");
    snprintf(b.sound_note, sizeof(b.sound_note), "pair XWF-M18 in Bluetooth");
    return b;
}

static void test_path(void)
{
    char buf[256];
    CHECK(cp0_board_state_path("/run/user/1000", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/board.state") == 0);
    CHECK(cp0_board_state_path(NULL, buf, sizeof(buf)) == 0 && strcmp(buf, "/tmp/applaunch-board.state") == 0);
    char small[10];
    CHECK(cp0_board_state_path("/run/user/1000", small, sizeof(small)) == -1);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    unsetenv("APPLAUNCH_STATE_DIR");
    CHECK(cp0_app_state_path(CP0_APP_BOARD_FILE, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/board.state") == 0); /* the launcher's place is the apps' place */
    unsetenv("XDG_RUNTIME_DIR");
}

static void test_format_parse(void)
{
    char text[1536];
    cp0_app_board_t b = sample();
    const int n = cp0_board_state_format(&b, text, sizeof(text));
    CHECK(n > 0 && (size_t)n == strlen(text));
    char want[1536];
    snprintf(want, sizeof(want),
             "v=1\ngen=3\npid=%d\nlauncher=0.8.6\nboard=hackberry-cm5\narch=aarch64\nram_mb=1800\nbattery=1\n"
             "battery_source=gauge:max17048\nbacklight=levels\nbacklight_steps=10\nsound=1\nsound_kind=bluetooth\n"
             "sound_guess=1\nrequests=hello,caps,toast,beep,volume,brightness,dim\n"
             "sound_note=pair XWF-M18 in Bluetooth\n",
             (int)getpid());
    CHECK(strcmp(text, want) == 0);
    char tiny[32];
    CHECK(cp0_board_state_format(&b, tiny, sizeof(tiny)) == -1);

    cp0_app_board_t back;
    CHECK(cp0_app_board_parse(text, (size_t)n, &back) == 0);
    CHECK(back.pid == b.pid && back.gen == 3 && strcmp(back.launcher, "0.8.6") == 0);
    CHECK(strcmp(back.board, "hackberry-cm5") == 0 && strcmp(back.arch, "aarch64") == 0 && back.ram_mb == 1800);
    CHECK(back.battery == 1 && strcmp(back.battery_source, "gauge:max17048") == 0);
    CHECK(strcmp(back.backlight, "levels") == 0 && back.backlight_steps == 10);
    CHECK(back.sound == 1 && strcmp(back.sound_kind, "bluetooth") == 0 && back.sound_guess == 1);
    CHECK(strcmp(back.sound_note, "pair XWF-M18 in Bluetooth") == 0 && back.battery_note[0] == '\0');
    CHECK(cp0_app_board_has_request(&back, "beep") && cp0_app_board_has_request(&back, "dim"));
    CHECK(cp0_app_board_has_request(&back, "hello") && !cp0_app_board_has_request(&back, "hel"));
    CHECK(!cp0_app_board_has_request(&back, "") && !cp0_app_board_has_request(NULL, "beep"));

    /* a fact the launcher does not know is an absent key, not 0 */
    cp0_app_board_t sparse;
    memset(&sparse, 0, sizeof(sparse));
    sparse.pid = 5;
    sparse.ram_mb = -1;
    sparse.battery = -1;
    sparse.backlight_steps = -1;
    sparse.sound = -1;
    sparse.sound_guess = -1;
    CHECK(cp0_board_state_format(&sparse, text, sizeof(text)) > 0);
    CHECK(strcmp(text, "v=1\ngen=0\npid=5\n") == 0);
    CHECK(cp0_app_board_parse(text, strlen(text), &back) == 0);
    CHECK(back.ram_mb == -1 && back.battery == -1 && back.sound == -1 && back.sound_guess == -1 && back.board[0] == '\0');
    CHECK(back.backlight_steps == -1 && !cp0_app_board_has_request(&back, "beep"));
    /* sound present on a board with no sound output is a "0", not an absent key */
    sparse.sound = 0;
    CHECK(cp0_board_state_format(&sparse, text, sizeof(text)) > 0 && strstr(text, "sound=0\n"));

    /* control characters in a note never break the file apart */
    snprintf(b.sound_note, sizeof(b.sound_note), "a\nb=c");
    CHECK(cp0_board_state_format(&b, text, sizeof(text)) > 0 && strstr(text, "sound_note=a b=c\n"));

    /* tolerant reader: CRLF, unknown keys, out of range numbers keep "absent" */
    const char *odd = "v=1\r\nfuture=1\r\nram_mb=2048\r\nbattery=7\r\nsound=9\r\nsound_guess=2\r\npid=5\r\n";
    CHECK(cp0_app_board_parse(odd, strlen(odd), &back) == 0);
    CHECK(back.ram_mb == 2048 && back.battery == -1 && back.sound == -1 && back.sound_guess == -1 && back.pid == 5);

    /* not a v=1 file: refused, everything absent */
    CHECK(cp0_app_board_parse("v=2\nram_mb=5\n", 13, &back) == -1 && back.ram_mb == -1 && back.battery == -1);
    CHECK(cp0_app_board_parse("ram_mb=5\n", 9, &back) == -1);
    CHECK(cp0_app_board_parse(NULL, 0, &back) == -1 && back.sound == -1);

    /* differs ignores gen and pid, sees every fact */
    cp0_app_board_t x = sample(), y = sample();
    y.gen = 99;
    y.pid = 1234;
    CHECK(!cp0_board_state_differs(&x, &y));
    y.sound = 0;
    CHECK(cp0_board_state_differs(&x, &y));
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
    char root[] = "/tmp/cp0-board-state-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512], dir[512];
    CHECK(cp0_board_state_path(root, path, sizeof(path)) == 0);
    snprintf(dir, sizeof(dir), "%s/applaunch", root);

    cp0_app_board_t b = sample();
    CHECK(cp0_board_state_write(path, &b) == 0);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644);

    cp0_app_board_t back;
    CHECK(cp0_app_board_read(path, &back) == 0 && back.ram_mb == 1800 && back.gen == 3);
    setenv("APPLAUNCH_STATE_DIR", dir, 1);
    CHECK(cp0_app_board_read(NULL, &back) == 0 && back.sound == 1);

    b.gen = 4;
    b.sound = 0;
    CHECK(cp0_board_state_write(path, &b) == 0);
    CHECK(cp0_app_board_read(NULL, &back) == 0 && back.gen == 4 && back.sound == 0);
    CHECK(count_entries(dir) == 1); /* replaced, no temporary left */

    /* the launcher is gone: a child that has exited */
    const pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) _exit(0);
    int status = 0;
    CHECK(waitpid(child, &status, 0) == child);
    b.pid = (int)child;
    CHECK(cp0_board_state_write(path, &b) == 0);
    CHECK(cp0_app_board_read(NULL, &back) == -1 && back.ram_mb == -1 && back.pid == 0);
    CHECK(cp0_app_board_load(NULL, &back) == 0 && back.pid == (int)child);

    CHECK(cp0_app_board_read("/nonexistent/board.state", &back) == -1);
    CHECK(cp0_board_state_write("/proc/applaunch-test/board.state", &b) == -1);
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
    printf("test_board_state: PASS\n");
    return 0;
}
