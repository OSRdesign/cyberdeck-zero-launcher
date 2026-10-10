/*
 * Keyboard presence in the launcher (F1 phase 0): the launcher reads every real keyboard, USB or Bluetooth,
 * and publishes the input policy file for apps ($XDG_RUNTIME_DIR/applaunch/input.state, see
 * cp0_input_state.h). Device builds only; elsewhere these calls do nothing.
 *
 * APPLAUNCH_KEYBOARD_SCAN=0 in the service environment turns the multi-keyboard reading off (the launcher
 * then reads LV_LINUX_KEYBOARD_DEVICE only, as before 0.5.0); the policy file is published either way.
 */
#pragma once

namespace input_presence {

/* Before cp0_lvgl_run(): lets the keyboard thread read every keyboard (unless APPLAUNCH_KEYBOARD_SCAN=0). */
void configure();

/* Start / stop publishing the policy file from the presence watcher. */
void start();
void stop();

} // namespace input_presence
