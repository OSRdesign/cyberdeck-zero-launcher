/*
 * SPDX-License-Identifier: MIT
 *
 * What the board has, detected at run time (see cp0_capabilities.h).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* pipe2, scandir */
#endif
#include "cp0_capabilities.h"

#include "cp0_power_supply.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define MIXER_OUTPUT_SIZE 32768
#define MIXER_TIMEOUT_MS 2000

static const char *env_or(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value && value[0] ? value : fallback;
}

const char *cp0_caps_pactl(void) { return env_or(CP0_CAPS_PACTL_ENV, "pactl"); }

const char *cp0_caps_amixer(void) { return env_or(CP0_CAPS_AMIXER_ENV, "amixer"); }

/* The first line of dir/file without its line end (a fake tree made on Windows may have "\r\n"). */
static int read_line(const char *dir, const char *file, char *text, size_t size)
{
    char path[640];
    const int n = snprintf(path, sizeof(path), "%s/%s", dir, file);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    FILE *in = fopen(path, "r");
    if (!in) return 0;
    const int ok = fgets(text, (int)size, in) != NULL;
    fclose(in);
    if (!ok) return 0;
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' || text[len - 1] == ' ' || text[len - 1] == '\t'))
        text[--len] = '\0';
    return 1;
}

static int read_positive(const char *dir, const char *file, int *value)
{
    char text[32];
    if (!read_line(dir, file, text, sizeof(text)) || !text[0]) return 0;
    char *end = NULL;
    errno = 0;
    const long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed <= 0 || parsed > INT_MAX) return 0;
    *value = (int)parsed;
    return 1;
}

static int contains_hdmi(const char *text)
{
    for (; text && *text; ++text)
        if (tolower((unsigned char)text[0]) == 'h' && tolower((unsigned char)text[1]) == 'd' &&
            tolower((unsigned char)text[2]) == 'm' && tolower((unsigned char)text[3]) == 'i')
            return 1;
    return 0;
}

static int visible(const struct dirent *entry) { return entry->d_name[0] != '.'; }

/* byte order, not the locale's: the same device wins on every system */
static int by_name(const struct dirent **a, const struct dirent **b) { return strcmp((*a)->d_name, (*b)->d_name); }

/* ------------------------------------------------------------------------------------------------ backlight */

static void fill_backlight(cp0_caps_backlight_t *out, cp0_cap_backlight_kind_t kind, const char *dir, int fallback_max)
{
    snprintf(out->dir, sizeof(out->dir), "%s", dir);
    size_t n = strlen(out->dir);
    while (n > 1 && out->dir[n - 1] == '/') out->dir[--n] = '\0';
    out->kind = kind;
    if (!read_positive(out->dir, "max_brightness", &out->max)) out->max = fallback_max;
    char path[300];
    const int len = snprintf(path, sizeof(path), "%s/brightness", out->dir);
    out->writable = len > 0 && (size_t)len < sizeof(path) && access(path, W_OK) == 0;
}

/* "<prefix><dir>" with a non-empty dir that fits */
static int take_profile(const char *spec, const char *prefix, cp0_cap_backlight_kind_t kind, int fallback_max,
                        cp0_caps_backlight_t *out)
{
    const size_t len = strlen(prefix);
    if (strncmp(spec, prefix, len) != 0 || !spec[len] || strlen(spec + len) >= sizeof(out->dir)) return 0;
    fill_backlight(out, kind, spec + len, fallback_max);
    out->from_profile = 1;
    return 1;
}

/* the kernel's preference (Documentation/ABI/stable/sysfs-class-backlight): firmware, platform, raw */
static int type_rank(const char *dir)
{
    char text[32];
    if (!read_line(dir, "type", text, sizeof(text))) return 2;
    if (strcmp(text, "firmware") == 0) return 0;
    if (strcmp(text, "platform") == 0) return 1;
    return 2;
}

void cp0_caps_backlight_at(const char *spec, const char *root, cp0_caps_backlight_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->kind = CP0_BL_NONE;
    if (spec && spec[0]) {
        if (take_profile(spec, "gpio:", CP0_BL_ONOFF, 1, out)) return;
        if (take_profile(spec, "sysfs:", CP0_BL_LEVELS, 100, out)) return;
        if (strcmp(spec, "none") == 0) {
            out->from_profile = 1;
            return;
        }
        /* any other value: as if unset */
    }
    if (!root || !root[0]) root = env_or(CP0_CAPS_BACKLIGHT_ROOT_ENV, CP0_CAPS_BACKLIGHT_DEFAULT_ROOT);

    struct dirent **entries = NULL;
    const int count = scandir(root, &entries, visible, by_name);
    if (count < 0) return;
    char best[256] = "";
    int best_rank = 99, best_max = 0;
    for (int i = 0; i < count; ++i) {
        char dir[256];
        const int n = snprintf(dir, sizeof(dir), "%s/%s", root, entries[i]->d_name);
        int max = 0;
        if (n > 0 && (size_t)n < sizeof(dir) && read_positive(dir, "max_brightness", &max)) {
            const int rank = type_rank(dir);
            if (rank < best_rank) {
                best_rank = rank;
                best_max = max;
                memcpy(best, dir, (size_t)n + 1);
            }
        }
        free(entries[i]);
    }
    free(entries);
    if (best[0]) fill_backlight(out, best_max == 1 ? CP0_BL_ONOFF : CP0_BL_LEVELS, best, best_max);
}

void cp0_caps_backlight(cp0_caps_backlight_t *out) { cp0_caps_backlight_at(getenv("APPLAUNCH_BACKLIGHT"), NULL, out); }

/* ------------------------------------------------------------------------------------------------ programs */

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void reap(pid_t pid)
{
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

int cp0_caps_run(const char *const argv[], char *out, size_t size, int timeout_ms)
{
    if (out && size) out[0] = '\0';
    if (!argv || !argv[0] || !argv[0][0]) return -1;
    if (timeout_ms < 0) timeout_ms = 0;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return -1;

    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_init(&attr);
    /* the caller's thread may block signals: the program starts with none blocked and SIGPIPE at its default */
    sigset_t mask, defaults;
    sigemptyset(&mask);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attr, &mask);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);

    pid_t pid = -1;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, &attr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(fds[1]);
    if (spawned != 0) {
        close(fds[0]);
        return -1;
    }

    const long long deadline = now_ms() + timeout_ms;
    size_t used = 0;
    int timed_out = 0;
    for (;;) {
        const long long left = deadline - now_ms();
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        struct pollfd wait_for = {fds[0], POLLIN, 0};
        const int ready = poll(&wait_for, 1, (int)(left > INT_MAX ? INT_MAX : left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) {
            timed_out = 1;
            break;
        }
        char chunk[512];
        const ssize_t got = read(fds[0], chunk, sizeof(chunk));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break; /* the end of its output (or an error) */
        if (out && size > 1 && used < size - 1) {
            const size_t room = size - 1 - used;
            const size_t take = (size_t)got < room ? (size_t)got : room;
            memcpy(out + used, chunk, take);
            used += take;
            out[used] = '\0';
        }
    }
    close(fds[0]);
    if (!timed_out) {
        for (;;) {
            int status = 0;
            const pid_t done = waitpid(pid, &status, WNOHANG);
            if (done == pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            if (done < 0 && errno != EINTR) return -1;
            if (now_ms() >= deadline) break;
            const struct timespec pause = {0, 2 * 1000 * 1000};
            nanosleep(&pause, NULL);
        }
    }
    kill(pid, SIGKILL);
    reap(pid);
    return -1;
}

/* ------------------------------------------------------------------------------------------------ sound */

/* "pvolume" (playback volume) or "volume" (one volume for both directions) among the words after "Capabilities:" */
static int has_playback_volume(const char *capabilities)
{
    const char *cursor = capabilities;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        const char *word = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
        const size_t len = (size_t)(cursor - word);
        if ((len == 7 && strncmp(word, "pvolume", 7) == 0) || (len == 6 && strncmp(word, "volume", 6) == 0)) return 1;
    }
    return 0;
}

int cp0_caps_mixer_control(int card, char *control, size_t control_size)
{
    static const char *const kPreferred[] = {"Master", "Speaker", "PCM", "Headphone"};
    enum { kCount = sizeof(kPreferred) / sizeof(kPreferred[0]) };
    if (control && control_size) control[0] = '\0';
    if (card < 0) return 0;
    char card_text[16];
    snprintf(card_text, sizeof(card_text), "%d", card);
    const char *const argv[] = {cp0_caps_amixer(), "-c", card_text, NULL};
    char *output = malloc(MIXER_OUTPUT_SIZE);
    if (!output) return 0;
    if (cp0_caps_run(argv, output, MIXER_OUTPUT_SIZE, MIXER_TIMEOUT_MS) != 0) {
        free(output);
        return 0;
    }

    /* "Simple mixer control 'Master',0" then "  Capabilities: pvolume pswitch ..." */
    int found[kCount] = {0};
    int current = -1;
    for (char *line = strtok(output, "\n"); line; line = strtok(NULL, "\n")) {
        while (*line == ' ' || *line == '\t') ++line;
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = '\0';
        if (strncmp(line, "Simple mixer control '", 22) == 0) {
            current = -1;
            const char *name = line + 22;
            const char *quote = strchr(name, '\'');
            if (!quote || strcmp(quote, "',0") != 0) continue; /* index 0 only: the one "sset NAME" sets */
            for (int i = 0; i < kCount; ++i)
                if ((size_t)(quote - name) == strlen(kPreferred[i]) && strncmp(name, kPreferred[i], strlen(kPreferred[i])) == 0)
                    current = i;
        } else if (current >= 0 && strncmp(line, "Capabilities:", 13) == 0) {
            if (has_playback_volume(line + 13)) found[current] = 1;
            current = -1;
        }
    }
    free(output);
    for (int i = 0; i < kCount; ++i) {
        if (!found[i]) continue;
        if (control && control_size) snprintf(control, control_size, "%s", kPreferred[i]);
        return 1;
    }
    return 0;
}

/* /proc/asound/cardN has a playback PCM (pcm0p, pcm3p, ...) */
static int has_playback_pcm(const char *root, int card)
{
    char dir[600];
    const int n = snprintf(dir, sizeof(dir), "%s/card%d", root, card);
    if (n < 0 || (size_t)n >= sizeof(dir)) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    int found = 0;
    for (const struct dirent *entry = readdir(d); entry && !found; entry = readdir(d)) {
        const size_t len = strlen(entry->d_name);
        found = len > 4 && strncmp(entry->d_name, "pcm", 3) == 0 && entry->d_name[len - 1] == 'p';
    }
    closedir(d);
    return found;
}

int cp0_caps_local_output_at(const char *root, char *control, size_t control_size)
{
    if (control && control_size) control[0] = '\0';
    if (!root || !root[0]) root = env_or(CP0_CAPS_ASOUND_ROOT_ENV, CP0_CAPS_ASOUND_DEFAULT_ROOT);
    char path[600];
    const int n = snprintf(path, sizeof(path), "%s/cards", root);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    FILE *in = fopen(path, "r");
    if (!in) return -1;
    /* " 0 [PCH            ]: HDA-Intel - HDA Intel PCH" (then an indented long name line) */
    char line[256];
    int result = -1;
    while (result < 0 && fgets(line, sizeof(line), in)) {
        const char *cursor = line;
        while (*cursor == ' ') ++cursor;
        if (!isdigit((unsigned char)*cursor)) continue;
        char *end = NULL;
        const long index = strtol(cursor, &end, 10);
        while (end && *end == ' ') ++end;
        if (!end || *end != '[' || index < 0 || index > 64) continue;
        if (contains_hdmi(line)) continue;
        if (!has_playback_pcm(root, (int)index)) continue;
        if (cp0_caps_mixer_control((int)index, control, control_size)) result = (int)index;
    }
    fclose(in);
    return result;
}

int cp0_caps_server_sink(int timeout_ms)
{
    const char *const argv[] = {cp0_caps_pactl(), "list", "short", "sinks", NULL};
    char output[4096];
    if (cp0_caps_run(argv, output, sizeof(output), timeout_ms) != 0) return 0;
    /* "<index>\t<name>\t<driver>\t<format>\t<state>" */
    for (char *line = strtok(output, "\n"); line; line = strtok(NULL, "\n")) {
        char *name = strchr(line, '\t');
        if (!name) continue;
        ++name;
        char *end = strchr(name, '\t');
        if (end) *end = '\0';
        if (!name[0] || strcmp(name, "auto_null") == 0 || contains_hdmi(name)) continue;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------------------ probe */

typedef enum { HINT_BATTERY, HINT_BACKLIGHT, HINT_SOUND } hint_kind_t;

static int hint_note(const char *env, hint_kind_t kind, char *note, size_t size)
{
    const char *value = getenv(env);
    if (!value || !value[0]) return 0;
    if (kind == HINT_BACKLIGHT && strcmp(value, "button") == 0)
        snprintf(note, size, "use the button on the case");
    else if (kind == HINT_SOUND && strncmp(value, "bluetooth:", 10) == 0 && value[10])
        snprintf(note, size, "pair %s in Bluetooth", value + 10);
    else
        snprintf(note, size, "%s", value);
    return 1;
}

void cp0_caps_probe(cp0_caps_t *out, int server_timeout_ms)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->sound_card = -1;
    out->sound_server = -1;

    cp0_power_battery_t battery;
    out->battery = cp0_power_supply_battery(&battery) ? CP0_CAP_PRESENT : CP0_CAP_ABSENT;

    cp0_caps_backlight(&out->bl);
    out->backlight = out->bl.kind != CP0_BL_NONE ? CP0_CAP_PRESENT : CP0_CAP_ABSENT;

    out->sound_card = cp0_caps_local_output_at(NULL, out->sound_control, sizeof(out->sound_control));
    if (out->sound_card >= 0) {
        out->sound = CP0_CAP_PRESENT;
    } else if (server_timeout_ms >= 0) {
        out->sound_server = cp0_caps_server_sink(server_timeout_ms);
        out->sound = out->sound_server == 1 ? CP0_CAP_PRESENT : CP0_CAP_ABSENT;
    }

    if (out->battery == CP0_CAP_ABSENT &&
        hint_note(CP0_CAPS_HINT_BATTERY_ENV, HINT_BATTERY, out->note_battery, sizeof(out->note_battery)))
        out->battery = CP0_CAP_NOT_SET_UP;
    if (out->backlight == CP0_CAP_ABSENT &&
        hint_note(CP0_CAPS_HINT_BACKLIGHT_ENV, HINT_BACKLIGHT, out->note_backlight, sizeof(out->note_backlight)))
        out->backlight = CP0_CAP_NOT_SET_UP;
    if (out->sound == CP0_CAP_ABSENT &&
        hint_note(CP0_CAPS_HINT_SOUND_ENV, HINT_SOUND, out->note_sound, sizeof(out->note_sound)))
        out->sound = CP0_CAP_NOT_SET_UP;
}
