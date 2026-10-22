/*
 * SPDX-License-Identifier: MIT
 *
 * Report 030 (c-1): the run-time capabilities (cp0_capabilities.h) on fake trees and fake programs.
 *   - backlight: absent, empty, the Pi 5 HyperPixel's gpio-backlight (max 1: on/off), the deck's PWM "backlight"
 *     (levels), the tablet's intel_backlight (max 937) beside acpi_video0 (the kernel's preference: firmware first),
 *     a device without max_brightness, the profile's gpio: / sysfs: / none, an unknown profile value, writable;
 *   - sound: no /proc/asound, HDMI only (the Pi 5, the HackBerry), the tablet's HDA PCH card (Master), the Pi 3A+'s
 *     Headphones card (PCM) beside vc4hdmi, a capture-only card, a card with no playback volume, amixer missing; a
 *     sound server's sinks (auto_null, HDMI, a Bluetooth speaker, a server that does not answer in time);
 *   - the board hints (not set up) and the whole probe;
 *   - cp0_caps_run: exit status, output, truncation, a missing program, a timeout.
 */
#define _GNU_SOURCE /* mkdtemp, setenv */
#include "cp0_capabilities.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char g_base[256];
static int g_tree;

static const char *new_dir(char *path, size_t size)
{
    snprintf(path, size, "%s/t%d", g_base, ++g_tree);
    assert(mkdir(path, 0755) == 0);
    return path;
}

static void mkdirs(const char *path)
{
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0) assert(errno == EEXIST);
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0) assert(errno == EEXIST);
}

/* dir/file = text (dir made on demand) */
static void put(const char *dir, const char *file, const char *text)
{
    mkdirs(dir);
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *out = fopen(path, "w");
    assert(out);
    fputs(text, out);
    fclose(out);
}

static void put_script(const char *path, const char *body)
{
    FILE *out = fopen(path, "w");
    assert(out);
    fputs("#!/bin/sh\n", out);
    fputs(body, out);
    fclose(out);
    assert(chmod(path, 0755) == 0);
}

/* root/name: a backlight device */
static void put_backlight(const char *root, const char *name, const char *max, const char *type)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", root, name);
    if (max) put(dir, "max_brightness", max);
    put(dir, "brightness", "1\n");
    if (type) put(dir, "type", type);
}

static int file_contains(const char *path, const char *text)
{
    FILE *in = fopen(path, "r");
    if (!in) return 0;
    char buf[4096];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, in);
    fclose(in);
    buf[n] = '\0';
    return strstr(buf, text) != NULL;
}

static void test_backlight(void)
{
    char root[300], dir[512];
    cp0_caps_backlight_t bl;

    /* no directory, an empty one: no backlight (the HackBerry) */
    snprintf(root, sizeof(root), "%s/nothing-here", g_base);
    cp0_caps_backlight_at(NULL, root, &bl);
    assert(bl.kind == CP0_BL_NONE && bl.dir[0] == '\0' && bl.max == 0 && !bl.from_profile);
    cp0_caps_backlight_at("", new_dir(root, sizeof(root)), &bl);
    assert(bl.kind == CP0_BL_NONE);

    /* the Pi 5 HyperPixel: gpio-backlight "backlight", max 1: on/off */
    new_dir(root, sizeof(root));
    put_backlight(root, "backlight", "1\n", "raw\n");
    cp0_caps_backlight_at(NULL, root, &bl);
    snprintf(dir, sizeof(dir), "%s/backlight", root);
    assert(bl.kind == CP0_BL_ONOFF && strcmp(bl.dir, dir) == 0 && bl.max == 1 && !bl.from_profile);
    if (geteuid() != 0) {
        assert(bl.writable == 1);
        char path[600];
        snprintf(path, sizeof(path), "%s/brightness", dir);
        assert(chmod(path, 0444) == 0);
        cp0_caps_backlight_at(NULL, root, &bl);
        assert(bl.kind == CP0_BL_ONOFF && bl.writable == 0); /* still the backlight, not writable */
        assert(chmod(path, 0644) == 0);
    }

    /* the deck: PWM "backlight", levels (no type file: raw) */
    new_dir(root, sizeof(root));
    put_backlight(root, "backlight", "100\n", NULL);
    cp0_caps_backlight_at(NULL, root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && bl.max == 100);

    /* the tablet: intel_backlight (raw, 937) and acpi_video0 (firmware, 15): the kernel prefers firmware */
    new_dir(root, sizeof(root));
    put_backlight(root, "intel_backlight", "937\n", "raw\n");
    put_backlight(root, "acpi_video0", "15\n", "firmware\n");
    cp0_caps_backlight_at(NULL, root, &bl);
    snprintf(dir, sizeof(dir), "%s/acpi_video0", root);
    assert(bl.kind == CP0_BL_LEVELS && strcmp(bl.dir, dir) == 0 && bl.max == 15);
    /* platform before raw; raw ones in name order */
    put_backlight(root, "acpi_video0", "15\n", "raw\n");
    put_backlight(root, "zz_platform", "10\n", "platform\n");
    cp0_caps_backlight_at(NULL, root, &bl);
    snprintf(dir, sizeof(dir), "%s/zz_platform", root);
    assert(strcmp(bl.dir, dir) == 0);
    put_backlight(root, "zz_platform", "10\n", "raw\n");
    cp0_caps_backlight_at(NULL, root, &bl);
    snprintf(dir, sizeof(dir), "%s/acpi_video0", root);
    assert(strcmp(bl.dir, dir) == 0 && bl.max == 15);

    /* a device without max_brightness, or 0: not a backlight */
    new_dir(root, sizeof(root));
    put_backlight(root, "broken", NULL, NULL);
    put_backlight(root, "dark", "0\n", NULL);
    cp0_caps_backlight_at(NULL, root, &bl);
    assert(bl.kind == CP0_BL_NONE);
    put_backlight(root, "intel_backlight", "937\r\n", NULL); /* a Windows line end */
    cp0_caps_backlight_at(NULL, root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && bl.max == 937);

    /* the profile wins: gpio: / sysfs: / none (the scan is not looked at), even for a missing directory */
    snprintf(dir, sizeof(dir), "%s/intel_backlight", root);
    char spec[600];
    snprintf(spec, sizeof(spec), "gpio:%s/", dir);
    cp0_caps_backlight_at(spec, root, &bl);
    assert(bl.kind == CP0_BL_ONOFF && strcmp(bl.dir, dir) == 0 && bl.max == 937 && bl.from_profile);
    snprintf(spec, sizeof(spec), "sysfs:%s", dir);
    cp0_caps_backlight_at(spec, root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && bl.max == 937 && bl.from_profile);
    cp0_caps_backlight_at("sysfs:/nonexistent/x", root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && bl.max == 100 && bl.writable == 0); /* the settings service's default range */
    cp0_caps_backlight_at("gpio:/nonexistent/x", root, &bl);
    assert(bl.kind == CP0_BL_ONOFF && bl.max == 1);
    cp0_caps_backlight_at("none", root, &bl);
    assert(bl.kind == CP0_BL_NONE && bl.from_profile);
    /* an unknown value, "gpio:" without a directory: scanned as if unset */
    cp0_caps_backlight_at("pwm", root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && !bl.from_profile);
    cp0_caps_backlight_at("gpio:", root, &bl);
    assert(bl.kind == CP0_BL_LEVELS && !bl.from_profile);

    /* the environment: $APPLAUNCH_BACKLIGHT and $APPLAUNCH_BACKLIGHT_ROOT */
    setenv(CP0_CAPS_BACKLIGHT_ROOT_ENV, root, 1);
    unsetenv("APPLAUNCH_BACKLIGHT");
    cp0_caps_backlight(&bl);
    assert(bl.kind == CP0_BL_LEVELS && bl.max == 937);
    setenv("APPLAUNCH_BACKLIGHT", "none", 1);
    cp0_caps_backlight(&bl);
    assert(bl.kind == CP0_BL_NONE);
    unsetenv("APPLAUNCH_BACKLIGHT");
    unsetenv(CP0_CAPS_BACKLIGHT_ROOT_ENV);
}

/* ---------------------------------------------------------------- sound */

static char g_fake[300]; /* fake programs and their files: $FAKE_DIR */

static void fake_programs(void)
{
    char path[400];
    new_dir(g_fake, sizeof(g_fake));
    setenv("FAKE_DIR", g_fake, 1);
    /* amixer -c N: amixer-cN.txt; [-M] sget X: amixer-sget.txt; [-M] sset X V: logged; every call logged */
    snprintf(path, sizeof(path), "%s/amixer", g_fake);
    put_script(path,
               "echo \"$*\" >> \"$FAKE_DIR/amixer.log\"\n"
               "[ $# -eq 2 ] && { [ -f \"$FAKE_DIR/amixer-c$2.txt\" ] && cat \"$FAKE_DIR/amixer-c$2.txt\" && exit 0; exit 1; }\n"
               "for a in \"$@\"; do case $a in\n"
               "  sget) cat \"$FAKE_DIR/amixer-sget.txt\" 2>/dev/null && exit 0; exit 1 ;;\n"
               "  sset) echo \"$*\" > \"$FAKE_DIR/amixer-sset\"; exit 0 ;;\n"
               "esac; done\n"
               "exit 1\n");
    /* pactl: "down" = no server (exit 1); "slow" = hangs; list short sinks: pactl-sinks.txt */
    snprintf(path, sizeof(path), "%s/pactl", g_fake);
    put_script(path,
               "echo \"$*\" >> \"$FAKE_DIR/pactl.log\"\n"
               "[ -f \"$FAKE_DIR/pactl-down\" ] && exit 1\n"
               "[ -f \"$FAKE_DIR/pactl-slow\" ] && exec sleep 5\n"
               "case \"$1\" in\n"
               "  list) cat \"$FAKE_DIR/pactl-sinks.txt\"; exit 0 ;;\n"
               "  get-sink-volume) cat \"$FAKE_DIR/pactl-volume.txt\"; exit 0 ;;\n"
               "  set-sink-volume) echo \"$3\" > \"$FAKE_DIR/pactl-volume-set\"; exit 0 ;;\n"
               "  get-sink-mute) cat \"$FAKE_DIR/pactl-mute.txt\"; exit 0 ;;\n"
               "  set-sink-mute) echo \"$3\" > \"$FAKE_DIR/pactl-mute-set\"; exit 0 ;;\n"
               "esac\n"
               "exit 1\n");
    snprintf(path, sizeof(path), "%s/amixer", g_fake);
    setenv(CP0_CAPS_AMIXER_ENV, path, 1);
    snprintf(path, sizeof(path), "%s/pactl", g_fake);
    setenv(CP0_CAPS_PACTL_ENV, path, 1);
}

static void fake_file(const char *name, const char *text) { put(g_fake, name, text); }

static void fake_remove(const char *name)
{
    char path[400];
    snprintf(path, sizeof(path), "%s/%s", g_fake, name);
    unlink(path);
}

static int fake_log_has(const char *log, const char *text)
{
    char path[400];
    snprintf(path, sizeof(path), "%s/%s", g_fake, log);
    return file_contains(path, text);
}

static const char kHdaControls[] =
    "Simple mixer control 'Master',0\n"
    "  Capabilities: pvolume pvolume-joined pswitch pswitch-joined\n"
    "  Playback channels: Mono\n"
    "  Limits: Playback 0 - 87\n"
    "  Mono: Playback 52 [60%] [-26.25dB] [on]\n"
    "Simple mixer control 'Headphone',0\n"
    "  Capabilities: pvolume pswitch\n"
    "Simple mixer control 'Speaker',0\n"
    "  Capabilities: pvolume pswitch\n"
    "Simple mixer control 'PCM',0\n"
    "  Capabilities: pvolume\n"
    "Simple mixer control 'Capture',0\n"
    "  Capabilities: cvolume cswitch\n";

/* an asound tree: the cards file and, per card, cardN/<pcm> */
static void asound_card(const char *root, int card, const char *pcm)
{
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/card%d/%s", root, card, pcm);
    mkdirs(dir);
}

static void test_local_output(void)
{
    char root[300], control[32];

    /* no /proc/asound (no ALSA at all), an empty cards file */
    snprintf(root, sizeof(root), "%s/no-asound", g_base);
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1 && control[0] == '\0');
    new_dir(root, sizeof(root));
    put(root, "cards", "--- no soundcards ---\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);

    /* HDMI only (the Pi 5, the HackBerry: vc4hdmi0 / vc4hdmi1): no local output, amixer not even asked */
    new_dir(root, sizeof(root));
    put(root, "cards",
        " 0 [vc4hdmi0       ]: vc4-hdmi - vc4-hdmi-0\n"
        "                      vc4-hdmi-0\n"
        " 1 [vc4hdmi1       ]: vc4-hdmi - vc4-hdmi-1\n"
        "                      vc4-hdmi-1\n");
    asound_card(root, 0, "pcm0p");
    asound_card(root, 1, "pcm0p");
    fake_file("amixer-c0.txt", kHdaControls);
    fake_file("amixer-c1.txt", kHdaControls);
    fake_remove("amixer.log");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);
    assert(!fake_log_has("amixer.log", "-c"));
    /* a PC whose HDMI audio is a card of its own ("HDA Intel HDMI") */
    put(root, "cards", " 0 [HDMI           ]: HDA-Intel - HDA Intel HDMI\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);

    /* the tablet: HDA Intel PCH (analog and HDMI PCMs on one card), Master */
    new_dir(root, sizeof(root));
    put(root, "cards",
        " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
        "                      HDA Intel PCH at 0x90a10000 irq 126\n");
    asound_card(root, 0, "pcm0p");
    asound_card(root, 0, "pcm0c");
    asound_card(root, 0, "pcm3p");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == 0 && strcmp(control, "Master") == 0);
    assert(fake_log_has("amixer.log", "-c 0"));
    setenv(CP0_CAPS_ASOUND_ROOT_ENV, root, 1); /* the environment's root */
    assert(cp0_caps_local_output_at(NULL, control, sizeof(control)) == 0);
    unsetenv(CP0_CAPS_ASOUND_ROOT_ENV);

    /* the preference: Speaker when there is no Master, then PCM, then Headphone */
    fake_file("amixer-c0.txt", "Simple mixer control 'Headphone',0\n  Capabilities: pvolume\n"
                               "Simple mixer control 'PCM',0\n  Capabilities: volume\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == 0 && strcmp(control, "PCM") == 0);
    fake_file("amixer-c0.txt", "Simple mixer control 'Headphone',0\n  Capabilities: pvolume\n"
                               "Simple mixer control 'Speaker',0\n  Capabilities: pvolume pswitch\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == 0 && strcmp(control, "Speaker") == 0);
    /* no playback volume (a switch alone, a capture volume, index 1 only): no output */
    fake_file("amixer-c0.txt", "Simple mixer control 'Master',0\n  Capabilities: pswitch\n"
                               "Simple mixer control 'Capture',0\n  Capabilities: cvolume\n"
                               "Simple mixer control 'PCM',1\n  Capabilities: pvolume\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);
    /* amixer missing (alsa-utils not installed) or failing */
    fake_file("amixer-c0.txt", kHdaControls);
    setenv(CP0_CAPS_AMIXER_ENV, "/nonexistent/amixer", 1);
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);
    char path[400];
    snprintf(path, sizeof(path), "%s/amixer", g_fake);
    setenv(CP0_CAPS_AMIXER_ENV, path, 1);
    fake_remove("amixer-c0.txt");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == -1);

    /* the Pi 3A+: bcm2835 Headphones (PCM) beside vc4hdmi; a capture-only USB microphone first */
    new_dir(root, sizeof(root));
    put(root, "cards",
        " 0 [Mic            ]: USB-Audio - USB Microphone\n"
        "                      Some USB Microphone at usb-3f980000.usb-1.2, full speed\n"
        " 1 [vc4hdmi        ]: vc4-hdmi - vc4-hdmi\n"
        "                      vc4-hdmi\n"
        " 2 [Headphones     ]: bcm2835_headpho - bcm2835 Headphones\n"
        "                      bcm2835 Headphones\n");
    asound_card(root, 0, "pcm0c");
    asound_card(root, 1, "pcm0p");
    asound_card(root, 2, "pcm0p");
    fake_file("amixer-c0.txt", kHdaControls); /* would qualify, but the card has no playback PCM */
    fake_file("amixer-c2.txt", "Simple mixer control 'PCM',0\n  Capabilities: pvolume pvolume-joined pswitch\n");
    assert(cp0_caps_local_output_at(root, control, sizeof(control)) == 2 && strcmp(control, "PCM") == 0);
}

static void test_server(void)
{
    /* no server */
    fake_file("pactl-down", "");
    assert(cp0_caps_server_sink(500) == 0);
    fake_remove("pactl-down");
    /* PipeWire with nothing connected (the HackBerry before its speaker connects): auto_null only */
    fake_file("pactl-sinks.txt", "35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n");
    assert(cp0_caps_server_sink(500) == 0);
    /* an HDMI monitor's sink: not a sound output of the board (C2) */
    fake_file("pactl-sinks.txt", "47\talsa_output.platform-107c701400.hdmi.hdmi-stereo\tPipeWire\ts32le 2ch 48000Hz\tSUSPENDED\n");
    assert(cp0_caps_server_sink(500) == 0);
    /* the case speaker over Bluetooth */
    fake_file("pactl-sinks.txt", "35\tauto_null\tPipeWire\tfloat32le 2ch 48000Hz\tSUSPENDED\n"
                                 "52\tbluez_output.9E_0C_81_E6_5D_1A.1\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n");
    assert(cp0_caps_server_sink(500) == 1);
    /* the tablet's analog sink */
    fake_file("pactl-sinks.txt", "0\talsa_output.pci-0000_00_1f.3.analog-stereo\tmodule-alsa-card.c\ts16le 2ch 44100Hz\tIDLE\n");
    assert(cp0_caps_server_sink(500) == 1);
    /* no pactl at all (the Pi 5) */
    char path[400];
    setenv(CP0_CAPS_PACTL_ENV, "/nonexistent/pactl", 1);
    assert(cp0_caps_server_sink(500) == 0);
    snprintf(path, sizeof(path), "%s/pactl", g_fake);
    setenv(CP0_CAPS_PACTL_ENV, path, 1);
    /* a server that does not answer: given up after the timeout */
    fake_file("pactl-slow", "");
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(cp0_caps_server_sink(200) == 0);
    clock_gettime(CLOCK_MONOTONIC, &end);
    const long ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;
    assert(ms >= 150 && ms < 2000);
    fake_remove("pactl-slow");
}

static void test_run(void)
{
    char out[16];
    const char *const echo[] = {"/bin/sh", "-c", "printf 'hello'; exit 3", NULL};
    assert(cp0_caps_run(echo, out, sizeof(out), 2000) == 3 && strcmp(out, "hello") == 0);
    const char *const lots[] = {"/bin/sh", "-c", "printf '0123456789abcdefghij'", NULL};
    assert(cp0_caps_run(lots, out, sizeof(out), 2000) == 0 && strcmp(out, "0123456789abcde") == 0); /* cut */
    assert(cp0_caps_run(lots, NULL, 0, 2000) == 0);
    const char *const missing[] = {"/nonexistent/program", NULL};
    assert(cp0_caps_run(missing, out, sizeof(out), 2000) == -1 && out[0] == '\0');
    const char *const none[] = {NULL};
    assert(cp0_caps_run(none, out, sizeof(out), 2000) == -1);
    const char *const slow[] = {"/bin/sh", "-c", "sleep 5", NULL};
    assert(cp0_caps_run(slow, out, sizeof(out), 100) == -1);
    /* the output closed early, the program still running: the timeout still holds */
    const char *const lingering[] = {"/bin/sh", "-c", "exec >/dev/null; sleep 5", NULL};
    assert(cp0_caps_run(lingering, out, sizeof(out), 100) == -1);
}

static void test_probe(void)
{
    char power[300], backlight[300], asound[300];
    cp0_caps_t caps;
    memset(&caps, 0x5a, sizeof(caps));

    /* nothing at all (a HackBerry before c-2, with its server not answering): all absent, no reason */
    setenv("APPLAUNCH_POWER_SUPPLY_ROOT", new_dir(power, sizeof(power)), 1);
    setenv(CP0_CAPS_BACKLIGHT_ROOT_ENV, new_dir(backlight, sizeof(backlight)), 1);
    setenv(CP0_CAPS_ASOUND_ROOT_ENV, new_dir(asound, sizeof(asound)), 1);
    unsetenv("APPLAUNCH_BACKLIGHT");
    fake_file("pactl-down", "");
    cp0_caps_probe(&caps, 500);
    assert(caps.battery == CP0_CAP_ABSENT && caps.backlight == CP0_CAP_ABSENT && caps.sound == CP0_CAP_ABSENT);
    assert(caps.bl.kind == CP0_BL_NONE && caps.sound_card == -1 && caps.sound_control[0] == '\0');
    assert(caps.sound_server == 0);
    assert(!caps.note_battery[0] && !caps.note_backlight[0] && !caps.note_sound[0]);
    /* the server not asked */
    cp0_caps_probe(&caps, -1);
    assert(caps.sound == CP0_CAP_ABSENT && caps.sound_server == -1);

    /* the hints: not set up, with the reason */
    setenv(CP0_CAPS_HINT_BACKLIGHT_ENV, "button", 1);
    setenv(CP0_CAPS_HINT_SOUND_ENV, "bluetooth:XWF-M18-M28-M38", 1);
    setenv(CP0_CAPS_HINT_BATTERY_ENV, "the gauge is not read yet", 1);
    cp0_caps_probe(&caps, 500);
    assert(caps.backlight == CP0_CAP_NOT_SET_UP && strcmp(caps.note_backlight, "use the button on the case") == 0);
    assert(caps.sound == CP0_CAP_NOT_SET_UP && strcmp(caps.note_sound, "pair XWF-M18-M28-M38 in Bluetooth") == 0);
    assert(caps.battery == CP0_CAP_NOT_SET_UP && strcmp(caps.note_battery, "the gauge is not read yet") == 0);
    setenv(CP0_CAPS_HINT_BACKLIGHT_ENV, "dimmer knob on the side", 1); /* any other text: as it is */
    setenv(CP0_CAPS_HINT_SOUND_ENV, "bluetooth:", 1);
    cp0_caps_probe(&caps, 500);
    assert(strcmp(caps.note_backlight, "dimmer knob on the side") == 0 && strcmp(caps.note_sound, "bluetooth:") == 0);

    /* a hint never makes it present, and what is there ignores its hint: the speaker connected */
    fake_remove("pactl-down");
    fake_file("pactl-sinks.txt", "52\tbluez_output.9E_0C_81_E6_5D_1A.1\tPipeWire\ts16le 2ch 48000Hz\tIDLE\n");
    put_backlight(backlight, "backlight", "1\n", NULL);
    cp0_caps_probe(&caps, 500);
    assert(caps.sound == CP0_CAP_PRESENT && caps.sound_server == 1 && caps.sound_card == -1 && !caps.note_sound[0]);
    assert(caps.backlight == CP0_CAP_PRESENT && caps.bl.kind == CP0_BL_ONOFF && !caps.note_backlight[0]);
    unsetenv(CP0_CAPS_HINT_BACKLIGHT_ENV);
    unsetenv(CP0_CAPS_HINT_SOUND_ENV);
    unsetenv(CP0_CAPS_HINT_BATTERY_ENV);

    /* the tablet: battery, levels, the local card (the server not asked) */
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/BAT1", power);
    put(dir, "type", "Battery\n");
    put(dir, "capacity", "80\n");
    put(asound, "cards", " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n");
    asound_card(asound, 0, "pcm0p");
    fake_file("amixer-c0.txt", kHdaControls);
    fake_remove("pactl.log");
    setenv("APPLAUNCH_BACKLIGHT", "sysfs:/sys/class/backlight/intel_backlight", 1);
    cp0_caps_probe(&caps, 500);
    assert(caps.battery == CP0_CAP_PRESENT && caps.backlight == CP0_CAP_PRESENT && caps.bl.kind == CP0_BL_LEVELS);
    assert(caps.sound == CP0_CAP_PRESENT && caps.sound_card == 0 && strcmp(caps.sound_control, "Master") == 0);
    assert(caps.sound_server == -1 && !fake_log_has("pactl.log", "list"));
    unsetenv("APPLAUNCH_BACKLIGHT");
    unsetenv("APPLAUNCH_POWER_SUPPLY_ROOT");
    unsetenv(CP0_CAPS_BACKLIGHT_ROOT_ENV);
    unsetenv(CP0_CAPS_ASOUND_ROOT_ENV);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_base, sizeof(g_base), "%s/cp0-caps-XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
    assert(mkdtemp(g_base));
    fake_programs();

    test_run();
    test_backlight();
    test_local_output();
    test_server();
    test_probe();

    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_base);
    if (system(cmd) != 0) return 1;
    puts("capabilities OK");
    return 0;
}
