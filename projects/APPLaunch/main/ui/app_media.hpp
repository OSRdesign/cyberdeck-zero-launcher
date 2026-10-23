/*
 * SPDX-License-Identifier: MIT
 *
 * Volume, brightness, beeps, toasts and idle dimming for apps (report 035): what the launcher does for an app that is in
 * front (its raw volume and brightness keys, the requests of launcher.sock, the levels in status.state), on threads that
 * are not the LVGL thread. Every operation here is serialised, so a key, a request and the status feed never write the
 * mixer or the backlight at the same time. Device builds do the work; elsewhere the functions do nothing and say
 * "absent".
 */
#pragma once

#include <cstdint>
#include <string>

namespace app_media {

/* Starts / stops the worker thread (raw keys, idle dimming). After the audio and settings services exist; stop() before
 * they go. */
void start();
void stop();

/* ---- what the board has (cached for 10 s; a Bluetooth speaker that connects shows within that) */
bool sound_present();
bool backlight_present();
/* "levels", "onoff" or "none" */
const char *backlight_kind();
/* Idle dimming is offered on a battery board with brightness levels. */
bool dim_eligible();

/* ---- levels. Each returns the level now in force, or -1 when the board has no such thing / the backend failed. */
int volume_get(bool &muted);
int volume_set(int percent);
int volume_step(int direction); /* +1 / -1: one step of the volume key */
int volume_mute(bool muted);    /* the volume in force; muted is reported by volume_get */
int brightness_get();
int brightness_set(int percent); /* levels backlights only */

struct Levels {
    int volume = -1;
    int mute = 0;
    int brightness_pct = -1;
};
/* Reads the levels for status.state: volume -1 without sound, brightness_pct -1 without a backlight. */
Levels read_levels();
/* Increases whenever this module changed a level (the status feed then reads again at once). */
unsigned levels_generation();

/* ---- beeps: 0 played / queued, 1 no sound output, 2 unknown name. Plays at the volume in force, whatever the
 * launcher's own interface sounds are set to. */
int beep(const std::string &name);

/* ---- toasts: the text waits for the launcher to be in front (the legacy path blocks the LVGL thread; a fullscreen app
 * hides the launcher) and shows on the launcher's toast. At most 4 wait; the oldest goes. Any thread. */
void toast_post(const std::string &text);
/* Takes the waiting texts; false when none. LVGL thread. */
bool toast_take(std::string &text);
/* Creates the LVGL timer that shows the waiting texts. LVGL thread, after launcher_ui::init(). */
void toast_install_pump();

/* ---- the app in front */
/* The launcher hands the panel to an app (true) or takes it back (false): ui_screensaver_set_foreground(). Resets the
 * app's dim permission and un-dims. */
void set_app_front(bool front);
/* A raw key event (code, 1 press / 0 release / 2 repeat) read while an app is in front: volume, brightness and mute
 * keys are acted on, any key counts as activity. Cheap, any thread. */
void raw_key(int code, int value);
void raw_touch();
/* The raw readers are running (the supervisor's passive reader, the legacy runner's watcher): idle dimming needs it. */
void raw_session(bool on);
/* The app in front allows (true) or forbids (false) dimming when nothing is touched for the launcher's dark time.
 * False when the board does not offer it (dim_eligible()) or no raw reader runs. */
bool set_dim_allowed(bool allow);

} // namespace app_media
