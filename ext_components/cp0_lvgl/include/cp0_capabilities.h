/*
 * SPDX-License-Identifier: MIT
 *
 * What the board has, detected at run time (report 030, decisions C1-C4): a battery, a backlight (and what kind), a
 * sound output. Settings and the volume / brightness keys read it instead of compile-time switches. Plain C, no LVGL,
 * no service, no thread.
 *
 * Battery: exactly cp0_power_supply_battery() (cp0_power_supply.h).
 *
 * Backlight (cp0_caps_backlight_at):
 *   - $APPLAUNCH_BACKLIGHT wins: "gpio:<dir>" on/off, "sysfs:<dir>" levels, "none" no backlight (a board whose only
 *     device drives nothing);
 *   - otherwise (unset, or a value it does not know) the devices of /sys/class/backlight are scanned: the kernel's
 *     preference by `type` (firmware, then platform, then raw; a device without `type` counts as raw), then name order.
 *     A device needs a readable max_brightness above 0. max_brightness 1 = on/off only (a gpio-backlight: the Pi 5
 *     HyperPixel), more = levels. No device: none.
 *   `writable`: this process may write its `brightness` (access W_OK).
 *
 * Sound (cp0_caps_probe; decision C2):
 *   - a local output: the first ALSA card of /proc/asound/cards that is not HDMI (its id, driver and name do not
 *     contain "hdmi", any case: vc4hdmi, "HDA Intel HDMI"; the PC's mixed "HDA Intel PCH" counts), that has a playback
 *     PCM (/proc/asound/cardN/pcm*p) and a playback volume control among Master, Speaker, PCM, Headphone (in that
 *     order; `amixer -c N` lists them, $APPLAUNCH_AMIXER or "amixer");
 *   - otherwise a sound server's sink (`pactl list short sinks`, $APPLAUNCH_PACTL or "pactl"): any sink but auto_null
 *     and HDMI ones, a Bluetooth speaker included. Only when the caller allows it (server_timeout_ms >= 0): it runs a
 *     program, which may take up to the timeout.
 *   HDMI-only boards have no sound output.
 *
 * Not set up (decision C4): a hint in the board profile turns ABSENT into NOT_SET_UP with a reason (`note_*`), never into
 * PRESENT: $APPLAUNCH_HINT_BATTERY, $APPLAUNCH_HINT_BACKLIGHT ("button": "use the button on the case"),
 * $APPLAUNCH_HINT_SOUND ("bluetooth:<name>": "pair <name> in Bluetooth"); any other text is the reason as it is.
 *
 * Test roots: $APPLAUNCH_BACKLIGHT_ROOT replaces /sys/class/backlight, $APPLAUNCH_ASOUND_ROOT replaces /proc/asound,
 * $APPLAUNCH_POWER_SUPPLY_ROOT replaces /sys/class/power_supply (cp0_power_supply.h); $APPLAUNCH_PACTL and
 * $APPLAUNCH_AMIXER name the programs (fake scripts in tests, /bin/false in the render harness).
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CP0_CAPS_BACKLIGHT_ROOT_ENV "APPLAUNCH_BACKLIGHT_ROOT"
#define CP0_CAPS_BACKLIGHT_DEFAULT_ROOT "/sys/class/backlight"
#define CP0_CAPS_ASOUND_ROOT_ENV "APPLAUNCH_ASOUND_ROOT"
#define CP0_CAPS_ASOUND_DEFAULT_ROOT "/proc/asound"
#define CP0_CAPS_PACTL_ENV "APPLAUNCH_PACTL"
#define CP0_CAPS_AMIXER_ENV "APPLAUNCH_AMIXER"
#define CP0_CAPS_HINT_BATTERY_ENV "APPLAUNCH_HINT_BATTERY"
#define CP0_CAPS_HINT_BACKLIGHT_ENV "APPLAUNCH_HINT_BACKLIGHT"
#define CP0_CAPS_HINT_SOUND_ENV "APPLAUNCH_HINT_SOUND"

/* How long a sound server may take to answer (report 030 section 3.3). */
#define CP0_CAPS_SERVER_TIMEOUT_MS 500

typedef enum {
    CP0_CAP_ABSENT = 0,
    CP0_CAP_PRESENT = 1,
    CP0_CAP_NOT_SET_UP = 2, /* not there, but the board profile says it should be: note_* says why */
} cp0_cap_state_t;

typedef enum {
    CP0_BL_NONE = 0,
    CP0_BL_ONOFF = 1,  /* max_brightness 1, or gpio:<dir> */
    CP0_BL_LEVELS = 2, /* max_brightness above 1, or sysfs:<dir> */
} cp0_cap_backlight_kind_t;

typedef struct {
    cp0_cap_backlight_kind_t kind;
    char dir[256];    /* the device's sysfs directory, "" for none */
    int max;          /* max_brightness (0 for none; gpio: 1 and sysfs: 100 when it cannot be read) */
    int writable;     /* 1 when this process may write <dir>/brightness */
    int from_profile; /* 1 when $APPLAUNCH_BACKLIGHT named it (gpio:, sysfs: or none), 0 when found by the scan */
} cp0_caps_backlight_t;

typedef struct {
    cp0_cap_state_t battery;
    cp0_cap_state_t backlight;
    cp0_caps_backlight_t bl;
    cp0_cap_state_t sound;
    int sound_card;         /* the local output's ALSA card index, -1 none */
    char sound_control[32]; /* its playback control (Master, Speaker, PCM or Headphone), "" none */
    int sound_server;       /* -1 not asked, 0 no sink (or no answer), 1 a sink */
    char note_battery[96], note_backlight[96], note_sound[96]; /* the reason of a NOT_SET_UP, "" otherwise */
} cp0_caps_t;

/* The backlight for the profile value `spec` (NULL or "": scan) and the scan directory `root` (NULL or "": the
 * environment's, else the default). Always fills *out. */
void cp0_caps_backlight_at(const char *spec, const char *root, cp0_caps_backlight_t *out);
/* cp0_caps_backlight_at($APPLAUNCH_BACKLIGHT, NULL, out). */
void cp0_caps_backlight(cp0_caps_backlight_t *out);

/* The first local (not HDMI) ALSA card with a playback PCM and a playback volume control under `root` (NULL or "": the
 * environment's /proc/asound, else the default): its index (control in `control`), -1 when there is none. */
int cp0_caps_local_output_at(const char *root, char *control, size_t control_size);

/* The playback control of card `card` (amixer -c card): the first of Master, Speaker, PCM, Headphone with a playback
 * volume. 1 and its name in `control`, 0 when none or amixer could not run. */
int cp0_caps_mixer_control(int card, char *control, size_t control_size);

/* A sound server's sink other than auto_null and HDMI ones (pactl list short sinks, at most timeout_ms): 1 yes, 0 no
 * (or no server, no pactl, no answer in time). */
int cp0_caps_server_sink(int timeout_ms);

/* Everything above. server_timeout_ms < 0: the server is not asked (sound_server stays -1). */
void cp0_caps_probe(cp0_caps_t *out, int server_timeout_ms);

/* Runs argv (argv[0] looked up in PATH) with stdin and stderr on /dev/null and returns its exit status; its standard
 * output goes into `out` (NUL-terminated, cut at size - 1; out may be NULL). -1 when it could not be started, was killed
 * or did not end within timeout_ms (it is then killed). */
int cp0_caps_run(const char *const argv[], char *out, size_t size, int timeout_ms);

/* $APPLAUNCH_PACTL / $APPLAUNCH_AMIXER when set and not empty, else "pactl" / "amixer". */
const char *cp0_caps_pactl(void);
const char *cp0_caps_amixer(void);

#ifdef __cplusplus
}
#endif
