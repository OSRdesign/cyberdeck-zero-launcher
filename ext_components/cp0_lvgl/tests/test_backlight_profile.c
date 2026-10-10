/*
 * Unit test of the board-profile backlight (src/cp0_backlight_profile.c): default kind delegates
 * to cp0_backlight_read/max/write, gpio:<dir> switches <dir>/brightness between 0 and
 * max_brightness, unwritable files fail without crashing. The profile is read once per process,
 * so each environment runs in its own child.
 */
#define _POSIX_C_SOURCE 200809L

#include "cp0_backlight_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

    int failed = 0;
    failed += run(child_default, NULL);
    failed += run(child_default, "pwm");
    failed += run(child_default, "sysfs:/sys/class/backlight/backlight");
    failed += run(child_default, "gpio:");
    failed += run(child_gpio, dir);
    failed += run(missing_adapter, NULL);

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
