/*
 * Unit test of the board-profile backlight (src/cp0_backlight_profile.c): default kind delegates
 * to cp0_backlight_read/max/write, gpio:<dir> switches <dir>/brightness between 0 and
 * max_brightness, sysfs:<dir> writes levels 0..max_brightness (clamped) into <dir>/brightness,
 * unwritable files fail without crashing. The profile is read once per process, so each
 * environment runs in its own child.
 */
#define _POSIX_C_SOURCE 200809L

#include "cp0_backlight_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* stand-ins for the settings service (today's /sys/class/backlight/backlight path) */
static int stub_level = 37;
int cp0_backlight_read(void) { return stub_level; }
int cp0_backlight_max(void) { return 255; }
int cp0_backlight_write(int val)
{
    stub_level = val > 255 ? 255 : val;
    return stub_level;
}

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            _exit(1);                                                                \
        }                                                                            \
    } while (0)

static void write_file(const char *dir, const char *name, const char *text)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

static int read_file_int(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    int v = -1;
    CHECK(fscanf(f, "%d", &v) == 1);
    fclose(f);
    return v;
}

static void child_default(const char *spec)
{
    if (spec) setenv("APPLAUNCH_BACKLIGHT", spec, 1);
    else unsetenv("APPLAUNCH_BACKLIGHT");
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_DEFAULT);
    CHECK(strcmp(cp0_backlight_profile_dir(), "") == 0);
    CHECK(cp0_backlight_profile_get_max() == 255);
    CHECK(cp0_backlight_profile_get_brightness() == 37);
    CHECK(cp0_backlight_profile_is_on() == 1);
    CHECK(cp0_backlight_profile_set_brightness(120) == 120 && stub_level == 120);
    CHECK(cp0_backlight_profile_set_on(0) == 0 && stub_level == 0);
    CHECK(cp0_backlight_profile_is_on() == 0);
    CHECK(cp0_backlight_profile_set_on(1) == 0 && stub_level == 255);
    _exit(0);
}

static void child_gpio(const char *dir)
{
    char spec[600];
    snprintf(spec, sizeof(spec), "gpio:%s/", dir); /* trailing slash is trimmed */
    setenv("APPLAUNCH_BACKLIGHT", spec, 1);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF);
    CHECK(strcmp(cp0_backlight_profile_dir(), dir) == 0);
    CHECK(cp0_backlight_profile_get_max() == 1);
    CHECK(cp0_backlight_profile_is_on() == 0);
    CHECK(cp0_backlight_profile_set_on(1) == 0);
    CHECK(read_file_int(dir, "brightness") == 1);
    CHECK(cp0_backlight_profile_is_on() == 1);
    CHECK(cp0_backlight_profile_get_brightness() == 1);
    CHECK(cp0_backlight_profile_set_brightness(0) == 0);
    CHECK(read_file_int(dir, "brightness") == 0);
    CHECK(cp0_backlight_profile_set_brightness(200) == 1); /* any level > 0 = on */
    CHECK(read_file_int(dir, "brightness") == 1);
    CHECK(stub_level == 37); /* the default path was not touched */
    if (geteuid() != 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/brightness", dir);
        CHECK(chmod(path, 0444) == 0);
        CHECK(cp0_backlight_profile_set_on(0) == -1); /* not writable: fails quietly */
        CHECK(read_file_int(dir, "brightness") == 1);
        CHECK(chmod(path, 0644) == 0);
    }
    _exit(0);
}

/* sysfs:<dir> (the Pi 3A+ DSI regulator 10-0045, max 255; the tablet's intel_backlight, max 937): levels */
static void child_sysfs(const char *dir)
{
    char spec[600];
    snprintf(spec, sizeof(spec), "sysfs:%s//", dir); /* trailing slashes are trimmed */
    setenv("APPLAUNCH_BACKLIGHT", spec, 1);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_SYSFS);
    CHECK(strcmp(cp0_backlight_profile_dir(), dir) == 0);
    CHECK(cp0_backlight_profile_get_max() == 937);
    CHECK(cp0_backlight_profile_get_brightness() == 400);
    CHECK(cp0_backlight_profile_is_on() == 1);
    CHECK(cp0_backlight_profile_set_brightness(94) == 94); /* the 10 % step of the Settings slider */
    CHECK(read_file_int(dir, "brightness") == 94);
    CHECK(cp0_backlight_profile_get_brightness() == 94);
    CHECK(cp0_backlight_profile_set_brightness(5000) == 937); /* clamped to max_brightness */
    CHECK(read_file_int(dir, "brightness") == 937);
    CHECK(cp0_backlight_profile_set_brightness(-3) == 0); /* clamped to 0: off */
    CHECK(read_file_int(dir, "brightness") == 0);
    CHECK(cp0_backlight_profile_is_on() == 0);
    CHECK(cp0_backlight_profile_set_on(1) == 0); /* wake: full level */
    CHECK(read_file_int(dir, "brightness") == 937);
    CHECK(cp0_backlight_profile_set_on(0) == 0); /* sleep */
    CHECK(read_file_int(dir, "brightness") == 0);
    CHECK(stub_level == 37); /* the default path was not touched */
    if (geteuid() != 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/brightness", dir);
        CHECK(chmod(path, 0444) == 0);
        CHECK(cp0_backlight_profile_set_brightness(300) == -1); /* not writable: fails quietly */
        CHECK(read_file_int(dir, "brightness") == 0);
        CHECK(chmod(path, 0644) == 0);
    }
    _exit(0);
}

static void child_sysfs_missing(void)
{
    setenv("APPLAUNCH_BACKLIGHT", "sysfs:/nonexistent/intel_backlight", 1);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_SYSFS);
    CHECK(cp0_backlight_profile_get_max() == 100); /* unreadable: the settings service's default range */
    CHECK(cp0_backlight_profile_get_brightness() == -1);
    CHECK(cp0_backlight_profile_is_on() == -1);
    CHECK(cp0_backlight_profile_set_brightness(50) == -1);
    CHECK(stub_level == 37);
    _exit(0);
}

static void child_gpio_missing(void)
{
    setenv("APPLAUNCH_BACKLIGHT", "gpio:/nonexistent/backlight_gpio", 1);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF);
    CHECK(cp0_backlight_profile_get_max() == 1);
    CHECK(cp0_backlight_profile_get_brightness() == -1);
    CHECK(cp0_backlight_profile_is_on() == -1);
    CHECK(cp0_backlight_profile_set_on(1) == -1);
    _exit(0);
}

/* Report 030 (c-1): no profile value, detection in $APPLAUNCH_BACKLIGHT_ROOT (set by main to `root`). */

/* the Pi 5 HyperPixel: one gpio-backlight named "backlight", max 1: on/off, honest (no 10 levels that all write 1) */
static void child_detect_onoff(const char *root)
{
    unsetenv("APPLAUNCH_BACKLIGHT");
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/backlight", root);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_GPIO_ONOFF);
    CHECK(strcmp(cp0_backlight_profile_dir(), dir) == 0);
    CHECK(cp0_backlight_profile_get_max() == 1);
    CHECK(cp0_backlight_profile_set_on(0) == 0 && read_file_int(dir, "brightness") == 0);
    CHECK(cp0_backlight_profile_set_brightness(200) == 1 && read_file_int(dir, "brightness") == 1);
    CHECK(stub_level == 37); /* the settings service's default path was not used */
    _exit(0);
}

/* a levels device under another name (an unknown board's DSI regulator): as sysfs:<dir> */
static void child_detect_sysfs(const char *root)
{
    unsetenv("APPLAUNCH_BACKLIGHT");
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/10-0045", root);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_SYSFS);
    CHECK(strcmp(cp0_backlight_profile_dir(), dir) == 0);
    CHECK(cp0_backlight_profile_get_max() == 255);
    CHECK(cp0_backlight_profile_set_brightness(26) == 26 && read_file_int(dir, "brightness") == 26);
    _exit(0);
}

/* no backlight device (the HackBerry), or "none" in the profile: every call fails, nothing is written */
static void child_detect_none(const char *spec)
{
    if (spec) setenv("APPLAUNCH_BACKLIGHT", spec, 1);
    else unsetenv("APPLAUNCH_BACKLIGHT");
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_NONE);
    CHECK(strcmp(cp0_backlight_profile_dir(), "") == 0);
    CHECK(cp0_backlight_profile_get_max() == -1);
    CHECK(cp0_backlight_profile_get_brightness() == -1);
    CHECK(cp0_backlight_profile_is_on() == -1);
    CHECK(cp0_backlight_profile_set_brightness(50) == -1);
    CHECK(cp0_backlight_profile_set_on(1) == -1);
    CHECK(stub_level == 37);
    _exit(0);
}

/* none found at first, the driver's device appears later (the deck's PWM backlight slower than the launcher): found
 * again within the rescan interval (3 s); "none" in the profile stays none */
static void child_detect_late(const char *root)
{
    unsetenv("APPLAUNCH_BACKLIGHT");
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/backlight", root);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_NONE);
    CHECK(mkdir(dir, 0755) == 0);
    write_file(dir, "max_brightness", "255\n");
    write_file(dir, "brightness", "200\n");
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_NONE); /* not looked for again at once */
    const struct timespec pause = {3, 300 * 1000 * 1000};
    nanosleep(&pause, NULL);
    CHECK(cp0_backlight_profile_kind() == CP0_BACKLIGHT_KIND_DEFAULT); /* the deck's levels device: the default path */
    CHECK(cp0_backlight_profile_get_max() == 255);
    _exit(0);
}

/* The level set once at launcher start (cp0_backlight_startup_level): pure, no profile involved. `saved` is the raw
 * level Settings > Brightness keeps (<= 0: none). */
static int check_startup_level(void)
{
    const cp0_backlight_kind_t sysfs = CP0_BACKLIGHT_KIND_SYSFS;
    /* nothing saved, the last session ended dim (the Pi 5 HyperPixel pwm, 0..10): full */
    CHECK(cp0_backlight_startup_level(sysfs, 10, 1, -1) == 10);
    CHECK(cp0_backlight_startup_level(sysfs, 10, 0, 0) == 10);
    CHECK(cp0_backlight_startup_level(sysfs, 10, -1, -1) == 10); /* current unknown: full */
    CHECK(cp0_backlight_startup_level(sysfs, 10, 4, -1) == 10);  /* 40 %: below half */
    /* nothing saved, already at least half: left alone */
    CHECK(cp0_backlight_startup_level(sysfs, 10, 5, -1) == -1);
    CHECK(cp0_backlight_startup_level(sysfs, 10, 10, -1) == -1);
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_DEFAULT, 255, 128, 0) == -1);
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_DEFAULT, 255, 127, 0) == 255);
    /* a saved level (40 % on max 10 = level 4): that level, whatever the current one */
    CHECK(cp0_backlight_startup_level(sysfs, 10, 10, 4) == 4);
    CHECK(cp0_backlight_startup_level(sysfs, 10, 1, 4) == 4);
    CHECK(cp0_backlight_startup_level(sysfs, 10, -1, 4) == 4);
    CHECK(cp0_backlight_startup_level(sysfs, 10, 4, 4) == -1); /* already there: no write */
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_DEFAULT, 255, 0, 64) == 64); /* the deck */
    CHECK(cp0_backlight_startup_level(sysfs, 937, 937, 94) == 94);                     /* the tablet */
    /* clamp: a saved level above the device's maximum */
    CHECK(cp0_backlight_startup_level(sysfs, 10, 2, 255) == 10);
    CHECK(cp0_backlight_startup_level(sysfs, 10, 10, 255) == -1);
    /* on/off only (gpio, or a levels kind with max_brightness 1): never touched */
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_GPIO_ONOFF, 1, 0, -1) == -1);
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_GPIO_ONOFF, 1, 0, 1) == -1);
    CHECK(cp0_backlight_startup_level(sysfs, 1, 0, -1) == -1);
    CHECK(cp0_backlight_startup_level(sysfs, 0, 0, 4) == -1);
    /* no backlight */
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_NONE, -1, -1, -1) == -1);
    CHECK(cp0_backlight_startup_level(CP0_BACKLIGHT_KIND_NONE, 255, 0, 64) == -1);
    return 0;
}

static int run(void (*fn)(const char *), const char *arg)
{
    const pid_t pid = fork();
    if (pid == 0) fn(arg);
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) != pid) return 1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}

static void missing_adapter(const char *unused)
{
    (void)unused;
    child_gpio_missing();
}

static void sysfs_missing_adapter(const char *unused)
{
    (void)unused;
    child_sysfs_missing();
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/cp0-bl-test.%d", tmp && tmp[0] ? tmp : "/tmp", (int)getpid());
    if (mkdir(dir, 0755) != 0) {
        perror("mkdir");
        return 1;
    }
    write_file(dir, "max_brightness", "1\n");
    write_file(dir, "brightness", "0\n");

    /* the scan directory of an unset profile ($APPLAUNCH_BACKLIGHT_ROOT): the deck's PWM "backlight" (levels, 255),
     * which keeps the default path through the settings service */
    char scan[600], device[700];
    snprintf(scan, sizeof(scan), "%s.scan", dir);
    snprintf(device, sizeof(device), "%s/backlight", scan);
    if (mkdir(scan, 0755) != 0 || mkdir(device, 0755) != 0) {
        perror("mkdir");
        return 1;
    }
    write_file(device, "max_brightness", "255\n");
    write_file(device, "brightness", "200\n");
    setenv("APPLAUNCH_BACKLIGHT_ROOT", scan, 1);

    int failed = check_startup_level();
    failed += run(child_default, NULL);
    failed += run(child_default, "pwm");
    failed += run(child_default, "gpio:");
    failed += run(child_default, "sysfs:");
    failed += run(child_default, "levels:/sys/class/backlight/backlight");
    failed += run(child_gpio, dir);
    failed += run(missing_adapter, NULL);

    /* the same mock directory as an intel_backlight: max 937, lit at 400 */
    write_file(dir, "max_brightness", "937\n");
    write_file(dir, "brightness", "400\n");
    failed += run(child_sysfs, dir);
    failed += run(sysfs_missing_adapter, NULL);

    /* detection: max 1 = on/off; another name with levels = sysfs; nothing, or "none" = no backlight */
    write_file(device, "max_brightness", "1\n");
    write_file(device, "brightness", "1\n");
    failed += run(child_detect_onoff, scan);
    failed += run(child_detect_none, "none");
    char other[800];
    snprintf(other, sizeof(other), "%s/10-0045", scan);
    write_file(device, "max_brightness", "0\n"); /* not a usable device any more */
    if (mkdir(other, 0755) != 0) {
        perror("mkdir");
        return 1;
    }
    write_file(other, "max_brightness", "255\n");
    write_file(other, "brightness", "255\n");
    failed += run(child_detect_sysfs, scan);
    setenv("APPLAUNCH_BACKLIGHT_ROOT", dir, 1); /* a directory with no device in it */
    failed += run(child_detect_none, NULL);
    setenv("APPLAUNCH_BACKLIGHT_ROOT", "/nonexistent/backlight", 1);
    failed += run(child_detect_none, NULL);
    char late[800];
    snprintf(late, sizeof(late), "%s/late", scan);
    if (mkdir(late, 0755) != 0) {
        perror("mkdir");
        return 1;
    }
    setenv("APPLAUNCH_BACKLIGHT_ROOT", late, 1);
    failed += run(child_detect_late, late);

    char cleanup[900];
    snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", scan);
    if (system(cleanup) != 0) failed += 1;

    char path[600];
    snprintf(path, sizeof(path), "%s/brightness", dir);
    unlink(path);
    snprintf(path, sizeof(path), "%s/max_brightness", dir);
    unlink(path);
    rmdir(dir);

    if (failed) {
        fprintf(stderr, "test_backlight_profile: %d case(s) failed\n", failed);
        return 1;
    }
    printf("test_backlight_profile: ok\n");
    return 0;
}
