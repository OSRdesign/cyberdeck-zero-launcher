/*
 * The status feed (report 032): status.state written by the launcher (cp0_status_state.c) and read back by the app
 * contract's reader (cp0_app_status_*): path, exact text, format / parse round trip, atomic write, a missing file, a
 * wrong version, unknown keys, and a launcher that is gone (stale pid) or alive (own pid, a pid of another user); the
 * SSH indicator's keys (ssh=, ssh_act=, 2026-10-30) and a file of an older launcher without them.
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
    s.volume = 40;
    s.mute = 0;
    s.brightness_pct = 70;
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

/* the longest status file: every number at its widest and 8 audio devices of 35 bytes (the most bt_audio.cpp writes) */
static cp0_app_status_t longest_for_audio(void)
{
    cp0_app_status_t l = sample();
    l.gen = 4294967295u;
    l.pid = 2147483647;
    l.wifi_pct = 100;
    l.battery = 100;
    l.volume = 100;
    l.brightness_pct = 100;
    l.ssh = 9999;
    l.ssh_act = 1;
    char *at = l.bt_audio_dev;
    for (int i = 0; i < 8; i++)
        at += snprintf(at, sizeof(l.bt_audio_dev) - (size_t)(at - l.bt_audio_dev), "%sAA_BB_CC_DD_EE_%02X:headset:1:a2dp",
                       i ? ";" : "", i);
    return l;
}

static void test_format_parse(void)
{
    char text[256];
    cp0_app_status_t s = sample();
    const int n = cp0_status_state_format(&s, text, sizeof(text));
    CHECK(n > 0 && (size_t)n == strlen(text));
    char want[256];
    snprintf(want, sizeof(want),
             "v=1\ngen=7\npid=%d\nwifi=1\nwifi_pct=72\neth=1\nbt=connected\nbattery=64\nbattery_state=Not charging\n"
             "volume=40\nmute=0\nbrightness_pct=70\nssh=0\nssh_act=0\n",
             (int)getpid());
    CHECK(strcmp(text, want) == 0);
    char tiny[20];
    CHECK(cp0_status_state_format(&s, tiny, sizeof(tiny)) == -1);

    cp0_app_status_t back;
    CHECK(cp0_app_status_parse(text, (size_t)n, &back) == 0);
    CHECK(back.pid == s.pid && back.wifi == 1 && back.wifi_pct == 72 && back.eth == 1);
    CHECK(back.bt == CP0_APP_BT_CONNECTED && back.battery == 64 && back.gen == 7);
    CHECK(strcmp(back.battery_state, "Not charging") == 0 && back.battery_charging == 0);
    CHECK(back.volume == 40 && back.mute == 0 && back.brightness_pct == 70);

    /* report 035: no sound output / no backlight (-1), muted; an older launcher's file has none of the three keys */
    cp0_app_status_t levels = s;
    levels.volume = -1;
    levels.mute = 1;
    levels.brightness_pct = -1;
    CHECK(cp0_status_state_format(&levels, text, sizeof(text)) > 0 && strstr(text, "volume=-1\nmute=1\nbrightness_pct=-1\n"));
    CHECK(cp0_app_status_parse(text, strlen(text), &back) == 0 && back.volume == -1 && back.mute == 1 && back.brightness_pct == -1);
    const char *old_file = "v=1\ngen=2\npid=9\nwifi=1\nbattery=50\n";
    CHECK(cp0_app_status_parse(old_file, strlen(old_file), &back) == 0);
    CHECK(back.volume == -1 && back.mute == 0 && back.brightness_pct == -1 && back.battery == 50);
    const char *bad = "v=1\nvolume=101\nmute=2\nbrightness_pct=-2\n";
    CHECK(cp0_app_status_parse(bad, strlen(bad), &back) == 0 && back.volume == -1 && back.mute == 0 && back.brightness_pct == -1);

    /* the SSH indicator: the count and the activity flag (never an address); no session writes ssh_act=0 whatever the
     * flag says; an older launcher's file has neither key (0, 0); out of range values keep 0 */
    cp0_app_status_t ssh = s;
    ssh.ssh = 2;
    ssh.ssh_act = 1;
    CHECK(cp0_status_state_format(&ssh, text, sizeof(text)) > 0 && strstr(text, "brightness_pct=70\nssh=2\nssh_act=1\n"));
    CHECK(cp0_app_status_parse(text, strlen(text), &back) == 0 && back.ssh == 2 && back.ssh_act == 1);
    ssh.ssh_act = 0;
    CHECK(cp0_status_state_format(&ssh, text, sizeof(text)) > 0 && strstr(text, "ssh=2\nssh_act=0\n"));
    CHECK(cp0_app_status_parse(text, strlen(text), &back) == 0 && back.ssh == 2 && back.ssh_act == 0);
    ssh.ssh = 0;
    ssh.ssh_act = 1;
    CHECK(cp0_status_state_format(&ssh, text, sizeof(text)) > 0 && strstr(text, "ssh=0\nssh_act=0\n"));
    ssh.ssh = -3;
    CHECK(cp0_status_state_format(&ssh, text, sizeof(text)) > 0 && strstr(text, "ssh=0\nssh_act=0\n"));
    CHECK(cp0_app_status_parse(old_file, strlen(old_file), &back) == 0 && back.ssh == 0 && back.ssh_act == 0);
    const char *bad_ssh = "v=1\nssh=-1\nssh_act=2\n";
    CHECK(cp0_app_status_parse(bad_ssh, strlen(bad_ssh), &back) == 0 && back.ssh == 0 && back.ssh_act == 0);
    const char *many = "v=1\nssh=16\nssh_act=1\n";
    CHECK(cp0_app_status_parse(many, strlen(many), &back) == 0 && back.ssh == 16 && back.ssh_act == 1);
    /* Bluetooth audio devices (2026-11-11): bt_audio_dev= is the last key, absent when empty; entries are
     * MAC:kind:connected[:profile] separated by ';' */
    cp0_app_status_t audio = s;
    CHECK(cp0_status_state_format(&audio, text, sizeof(text)) > 0 && !strstr(text, "bt_audio_dev"));
    snprintf(audio.bt_audio_dev, sizeof(audio.bt_audio_dev),
             "70_BF_92_53_6B_8B:headset:1:a2dp;9E_0C_81_E6_5D_1A:speaker:0;F8_66_5A_18_D3_69:headset:1:hfp");
    char wide[768];
    CHECK(cp0_status_state_format(&audio, wide, sizeof(wide)) > 0);
    CHECK(strstr(wide, "ssh_act=0\nbt_audio_dev=70_BF_92_53_6B_8B:headset:1:a2dp;9E_0C_81_E6_5D_1A:speaker:0;F8_66_5A_18_D3_69:headset:1:hfp\n"));
    CHECK(strcmp(wide + strlen(wide) - 4, "hfp\n") == 0); /* the last line */
    CHECK(cp0_app_status_parse(wide, strlen(wide), &back) == 0);
    CHECK(back.bt == CP0_APP_BT_CONNECTED && back.ssh == 0 && strcmp(back.bt_audio_dev, audio.bt_audio_dev) == 0);
    cp0_app_bt_audio_dev_t dev;
    CHECK(cp0_app_bt_audio_dev_get(&back, 0, &dev) == 0);
    CHECK(strcmp(dev.mac, "70_BF_92_53_6B_8B") == 0 && strcmp(dev.kind, "headset") == 0 && dev.connected == 1 &&
          strcmp(dev.profile, "a2dp") == 0);
    CHECK(cp0_app_bt_audio_dev_get(&back, 1, &dev) == 0);
    CHECK(strcmp(dev.mac, "9E_0C_81_E6_5D_1A") == 0 && strcmp(dev.kind, "speaker") == 0 && dev.connected == 0 &&
          dev.profile[0] == '\0');
    CHECK(cp0_app_bt_audio_dev_get(&back, 2, &dev) == 0 && strcmp(dev.profile, "hfp") == 0 && dev.connected == 1);
    CHECK(cp0_app_bt_audio_dev_get(&back, 3, &dev) == -1 && dev.mac[0] == '\0');
    CHECK(cp0_app_bt_audio_dev_get(&back, -1, &dev) == -1);
    CHECK(cp0_app_bt_audio_dev_get(NULL, 0, &dev) == -1);
    /* none: no entry; an older launcher's file: the same */
    CHECK(cp0_app_status_parse(old_file, strlen(old_file), &back) == 0 && back.bt_audio_dev[0] == '\0');
    CHECK(cp0_app_bt_audio_dev_get(&back, 0, &dev) == -1);
    /* growth: fields appended to an entry, and a longer profile, are skipped; a broken entry is no device */
    cp0_app_status_t grow;
    const char *future = "v=1\nbt_audio_dev=AA_BB_CC_DD_EE_FF:headphones:0:a2dp:le:x;broken;11_22_33_44_55_66:speaker:1\n";
    CHECK(cp0_app_status_parse(future, strlen(future), &grow) == 0);
    CHECK(cp0_app_bt_audio_dev_get(&grow, 0, &dev) == 0 && strcmp(dev.kind, "headphones") == 0 && strcmp(dev.profile, "a2dp") == 0);
    CHECK(cp0_app_bt_audio_dev_get(&grow, 1, &dev) == -1);
    CHECK(cp0_app_bt_audio_dev_get(&grow, 2, &dev) == 0 && strcmp(dev.mac, "11_22_33_44_55_66") == 0 && dev.profile[0] == '\0');
    /* a reader with the old 512-byte buffer loses only this key: the file is cut inside bt_audio_dev */
    char cut[512];
    memset(cut, 0, sizeof(cut));
    memcpy(cut, wide, 200 < strlen(wide) ? 200 : strlen(wide));
    CHECK(cp0_app_status_parse(cut, strlen(cut), &back) == 0 && back.wifi == 1 && back.battery == 64 && back.volume == 40);
    /* the writer's text with the most devices (8) fits the 768-byte write buffer and a 512-byte reader's file */
    cp0_app_status_t eight = longest_for_audio();
    char eightText[768];
    CHECK(cp0_status_state_format(&eight, eightText, sizeof(eightText)) > 0 && strlen(eightText) < 512);
    /* the longest file still fits the writer's buffer (cp0_status_state_write: 768 bytes) */
    cp0_app_status_t longest = sample();
    longest.gen = 4294967295u;
    longest.pid = 2147483647;
    longest.wifi_pct = 100;
    longest.battery = 100;
    longest.volume = 100;
    longest.brightness_pct = 100;
    longest.ssh = 9999;
    longest.ssh_act = 1;
    char big[320];
    CHECK(cp0_status_state_format(&longest, big, sizeof(big)) > 0);

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
    CHECK(back.volume == -1 && back.brightness_pct == -1);
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
