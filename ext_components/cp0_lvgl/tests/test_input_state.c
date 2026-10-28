/*
 * Unit test of the input policy file (src/cp0/cp0_input_state.c): path choice, exact text, parsing,
 * atomic write (directory created, no temporary left, mode 0644) and the last_seen rule over a
 * plug/unplug sequence.
 */
#define _GNU_SOURCE

#include "cp0_input_state.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                 \
        }                                                                            \
    } while (0)

static void test_path(void)
{
    char buf[256];
    CHECK(cp0_input_state_path("/run/user/1000", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/run/user/1000/applaunch/input.state") == 0);
    CHECK(cp0_input_state_path(NULL, buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-input.state") == 0);
    CHECK(cp0_input_state_path("", buf, sizeof(buf)) == 0);
    CHECK(strcmp(buf, "/tmp/applaunch-input.state") == 0);
    char small[10];
    CHECK(cp0_input_state_path("/run/user/1000", small, sizeof(small)) == -1);
}

static void test_format_parse(void)
{
    const cp0_input_state_t state = {.keyboards = 2, .last_seen = 1760000000, .gen = 7};
    char text[320];
    const int n = cp0_input_state_format(&state, text, sizeof(text));
    CHECK(n > 0);
    CHECK(strcmp(text, "v=1\nkeyboards=2\nlast_seen=1760000000\nbt_keyboards=0\nkb_gone=0\nbt_gone=0\nvkb=auto\n"
                       "pointer=off\nhand=0\ngen=7\n") == 0);
    char tiny[8];
    CHECK(cp0_input_state_format(&state, tiny, sizeof(tiny)) == -1);

    cp0_input_state_t parsed;
    CHECK(cp0_input_state_parse(text, (size_t)n, &parsed) == 0);
    CHECK(parsed.keyboards == 2 && parsed.last_seen == 1760000000 && parsed.gen == 7);
    CHECK(parsed.bt_keyboards == 0 && parsed.kb_gone == 0 && parsed.bt_gone == 0 && parsed.vkb == CP0_APP_VKB_AUTO);

    /* the app contract additions (task 014 P5): Bluetooth count, gone times (monotonic s), the rule in force */
    cp0_input_state_t full = {.keyboards = 1, .last_seen = 1760000000, .gen = 9};
    full.bt_keyboards = 1;
    full.kb_gone = 120;
    full.bt_gone = 130;
    full.vkb = CP0_APP_VKB_OFF;
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0);
    CHECK(strcmp(text, "v=1\nkeyboards=1\nlast_seen=1760000000\nbt_keyboards=1\nkb_gone=120\nbt_gone=130\nvkb=off\n"
                       "pointer=off\nhand=0\ngen=9\n") == 0);
    CHECK(cp0_input_state_parse(text, strlen(text), &parsed) == 0);
    CHECK(parsed.keyboards == 1 && parsed.bt_keyboards == 1 && parsed.kb_gone == 120 && parsed.bt_gone == 130 &&
          parsed.vkb == CP0_APP_VKB_OFF && parsed.gen == 9 && parsed.pointer_nav == 0);
    /* pointer navigation (Settings > System > Pointer navigation): on, read back; a missing line or a stray value is
     * off */
    full.pointer_nav = 1;
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0 && strstr(text, "\npointer=on\nhand=0\ngen=9\n") != NULL);
    CHECK(cp0_input_state_parse(text, strlen(text), &parsed) == 0 && parsed.pointer_nav == 1);
    const char *crlf_on = "v=1\r\npointer=on\r\ngen=4\r\n";
    CHECK(cp0_input_state_parse(crlf_on, strlen(crlf_on), &parsed) == 0 && parsed.pointer_nav == 1);
    const char *stray = "v=1\npointer=yes\ngen=4\n";
    CHECK(cp0_input_state_parse(stray, strlen(stray), &parsed) == 0 && parsed.pointer_nav == 0);
    full.pointer_nav = 0;
    /* hand (report 044: Handwriting + keyboard on the PDA profile): 1 read back; a missing line or a stray value is 0 */
    CHECK(parsed.hand == 0);
    full.hand = 1;
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0 && strstr(text, "\npointer=off\nhand=1\ngen=9\n") != NULL);
    CHECK(cp0_input_state_parse(text, strlen(text), &parsed) == 0 && parsed.hand == 1 && parsed.gen == 9);
    full.hand = 7; /* any non-zero value is written as 1 */
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0 && strstr(text, "\nhand=1\n") != NULL);
    full.hand = 0;
    const char *crlf_hand = "v=1\r\nhand=1\r\ngen=4\r\n";
    CHECK(cp0_input_state_parse(crlf_hand, strlen(crlf_hand), &parsed) == 0 && parsed.hand == 1);
    const char *stray_hand = "v=1\nhand=2\ngen=4\n";
    CHECK(cp0_input_state_parse(stray_hand, strlen(stray_hand), &parsed) == 0 && parsed.hand == 0);
    const char *no_hand = "v=1\nvkb=auto\ngen=4\n";
    CHECK(cp0_input_state_parse(no_hand, strlen(no_hand), &parsed) == 0 && parsed.hand == 0);
    full.vkb = CP0_APP_VKB_FORCE;
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0 && strstr(text, "\nvkb=force\n") != NULL);
    full.vkb = CP0_APP_VKB_ZONE; /* report 041: the PDA writing zone is the text input */
    CHECK(cp0_input_state_format(&full, text, sizeof(text)) > 0 && strstr(text, "\nvkb=zone\n") != NULL);
    CHECK(cp0_input_state_parse(text, strlen(text), &parsed) == 0 && parsed.vkb == CP0_APP_VKB_ZONE);
    /* the longest values still fit the writer's buffer */
    cp0_input_state_t big = {.keyboards = 4294967295u, .last_seen = INT64_MIN, .gen = 4294967295u};
    big.bt_keyboards = 4294967295u;
    big.kb_gone = INT64_MAX;
    big.bt_gone = INT64_MAX;
    big.vkb = CP0_APP_VKB_FORCE;
    big.pointer_nav = 0; /* "off": the longer word */
    CHECK(cp0_input_state_format(&big, text, sizeof(text)) > 0);

    /* an older file (no new keys): vkb reads auto, the rest 0; unknown vkb values keep auto; CRLF is tolerated */
    const char *later = "v=1\r\nkeyboards=1\r\nvkb=auto\r\ngen=3\r\n";
    CHECK(cp0_input_state_parse(later, strlen(later), &parsed) == 0);
    CHECK(parsed.keyboards == 1 && parsed.last_seen == 0 && parsed.gen == 3);
    const char *old = "v=1\nkeyboards=0\nlast_seen=5\ngen=2\n";
    CHECK(cp0_input_state_parse(old, strlen(old), &parsed) == 0);
    CHECK(parsed.vkb == CP0_APP_VKB_AUTO && parsed.bt_keyboards == 0 && parsed.kb_gone == 0 && parsed.gen == 2);
    const char *odd = "v=1\nvkb=sometimes\nkb_gone=-5\ngen=1\n";
    CHECK(cp0_input_state_parse(odd, strlen(odd), &parsed) == 0);
    CHECK(parsed.vkb == CP0_APP_VKB_AUTO && parsed.kb_gone == 0);
    /* other versions and garbage are refused */
    const char *v2 = "v=2\nkeyboards=1\n";
    CHECK(cp0_input_state_parse(v2, strlen(v2), &parsed) == -1);
    const char *none = "keyboards=1\n";
    CHECK(cp0_input_state_parse(none, strlen(none), &parsed) == -1);
    const char *bad = "v=1\nkeyboards=x\ngen=-4\n";
    CHECK(cp0_input_state_parse(bad, strlen(bad), &parsed) == 0);
    CHECK(parsed.keyboards == 0 && parsed.gen == 0);
}

static int count_entries(const char *dir)
{
    DIR *d = opendir(dir);
    CHECK(d != NULL);
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) n++;
    closedir(d);
    return n;
}

static void test_write(void)
{
    char root[] = "/tmp/cp0-input-state-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char path[512], dir[512];
    CHECK(cp0_input_state_path(root, path, sizeof(path)) == 0);
    snprintf(dir, sizeof(dir), "%s/applaunch", root);

    const mode_t old_umask = umask(077);
    cp0_input_state_t state = {.keyboards = 1, .last_seen = 1760000100, .gen = 1};
    CHECK(cp0_input_state_write(path, &state) == 0); /* creates <runtime>/applaunch */
    umask(old_umask);
    struct stat st;
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0644);
    CHECK(stat(dir, &st) == 0 && S_ISDIR(st.st_mode));
    cp0_input_state_t back;
    CHECK(cp0_input_state_read(path, &back) == 0);
    CHECK(back.keyboards == 1 && back.last_seen == 1760000100 && back.gen == 1);

    /* replaced, never appended, and no temporary file is left next to it */
    state.keyboards = 0;
    state.gen = 2;
    CHECK(cp0_input_state_write(path, &state) == 0);
    CHECK(cp0_input_state_read(path, &back) == 0 && back.keyboards == 0 && back.gen == 2);
    CHECK(count_entries(dir) == 1);
    char text[256];
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    const size_t len = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[len] = '\0';
    CHECK(strcmp(text, "v=1\nkeyboards=0\nlast_seen=1760000100\nbt_keyboards=0\nkb_gone=0\nbt_gone=0\nvkb=auto\n"
                       "pointer=off\nhand=0\ngen=2\n") == 0);

    /* unwritable place: error, no crash */
    CHECK(cp0_input_state_write("/proc/applaunch-test/input.state", &state) == -1);
    CHECK(cp0_input_state_read("/nonexistent/input.state", &back) == -1);

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

static void test_last_seen(void)
{
    /* boot without a keyboard: never seen */
    CHECK(cp0_input_state_next_last_seen(0, 0, 0, 100) == 0);
    /* USB keyboard plugged at t=200 */
    CHECK(cp0_input_state_next_last_seen(0, 0, 1, 200) == 200);
    /* the M4 joins at t=300 (the list changes, still present) */
    CHECK(cp0_input_state_next_last_seen(1, 200, 2, 300) == 300);
    /* both go away at t=900: last_seen is the moment they went */
    CHECK(cp0_input_state_next_last_seen(2, 300, 0, 900) == 900);
    /* a later write while still absent (a restarted launcher) keeps it */
    CHECK(cp0_input_state_next_last_seen(0, 900, 0, 5000) == 900);
}

int main(void)
{
    test_path();
    test_format_parse();
    test_write();
    test_last_seen();
    printf("test_input_state: PASS\n");
    return 0;
}
