/*
 * SPDX-License-Identifier: MIT
 *
 * The pointer navigation reader (device builds): one thread that reads the pointer devices of the presence watcher
 * (cp0_keyboard_presence.h) and turns their reports into keys through the pointer navigation core
 * (cp0_pointer_nav.h). Internal to the keyboard backend (cp0_lvgl_keyboard.c).
 *
 *   - started by init_input() when the process reads every keyboard (cp0_keyboard_set_read_all_keyboards: the
 *     launcher and the native apps), stopped by deinit_input();
 *   - on (opens the devices) only while pointer navigation is on (cp0_pointer_nav_set_enabled / input.state
 *     pointer= / APPLAUNCH_POINTER_NAV) and the keyboard is not paused (an external app runs): otherwise every
 *     pointer node is closed and every push or press is forgotten;
 *   - never grabs a device (no EVIOCGRAB): the nodes stay shared (the desktop, the app the launcher started);
 *   - its keys enter the keyboard queue like a physical key's (cp0_keyboard_inject: press + release), so the lock
 *     screen, the global shortcuts, LV_EVENT_KEYBOARD and the LVGL keypad all see them. In a text field
 *     (KBD_INPUT_CONTEXT_TEXT) Left / Right (the caret) and the options key (a control character in a terminal) are
 *     not sent; Up / Down and Enter are;
 *   - logs (SLOGI) the pointer devices found, the devices it reads, the state of the switch; APPLAUNCH_POINTER_DEBUG=1
 *     also logs every key it sends or holds back and the button.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void cp0_pointer_reader_start(void);
void cp0_pointer_reader_stop(void);
/* Re-evaluate at once (keyboard paused / resumed, the switch changed). Any thread; async-signal unsafe. */
void cp0_pointer_reader_wake(void);

/* The keyboard backend's pause flag (keyboard_pause / keyboard_resume, cp0_lvgl_keyboard.c). */
int cp0_keyboard_input_paused(void);

#ifdef __cplusplus
}
#endif
