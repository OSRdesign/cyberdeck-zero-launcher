/*
 * SPDX-License-Identifier: MIT
 *
 * The per-launch environment of the app contract (task 014 P5, report 023, cp0_app_contract.h): what the launcher
 * puts on the command line of every app it starts, so the app knows its mode, where its picture is on the screen,
 * the canvas it draws and where the state files are:
 *   APPLAUNCH_CONTRACT=1 APPLAUNCH_APP_MODE=compat|fullscreen APPLAUNCH_WINDOW=x,y,w,h APPLAUNCH_CANVAS=WxH
 *   APPLAUNCH_CANVAS_SCALE=N [APPLAUNCH_STATE_DIR=<dir>] APPLAUNCH_PDA=0
 * (APPLAUNCH_PDA=0: the PDA writing zone is the launcher's, report 043; an app never draws its own band, whatever the
 * launcher's own APPLAUNCH_PDA it would otherwise inherit)
 * and, for a landscape fullscreen app run turned on a portrait launcher (report 038 section 3.2, option b; the PDA
 * profile), the display variables of the panel's landscape canvas, which the app's own display backend and the panel
 * readers take instead of the inherited board profile's and the launcher's:
 *   APPLAUNCH_ROTATE=R APPLAUNCH_LOGICAL=WxH APPLAUNCH_SCREEN_W=W APPLAUNCH_SCREEN_H=H APPLAUNCH_SCREEN_ROTATE=R
 *   APPLAUNCH_SCREEN_CLASS=<class of WxH>
 * Pure (no LVGL, no system call) except export_for_current(); unit-tested by tests/test_app_launch_env.cpp.
 */
#pragma once

#include "cp0_ui_metrics.h"

#include <string>
#include <utility>
#include <vector>

namespace app_launch_env {

enum class Mode { Compat, Fullscreen };

struct Input {
    Mode mode = Mode::Compat;
    int win_x = 0, win_y = 0, win_w = 0, win_h = 0; // on the screen, logical landscape coordinates
    int canvas_w = 0, canvas_h = 0;                 // the pixels the app draws
    int canvas_scale = 1;                           // screen pixels per canvas pixel
    std::string state_dir;                          // "" = not exported (no XDG_RUNTIME_DIR)
    // Turned (fullscreen only): the panel seen in landscape. The window and canvas above are then that canvas.
    bool turned = false;
    int turned_rotate = 0;                          // APPLAUNCH_ROTATE and APPLAUNCH_SCREEN_ROTATE
    std::string turned_class;                       // APPLAUNCH_SCREEN_CLASS of the landscape canvas
};

using Vars = std::vector<std::pair<std::string, std::string>>;

/* The variables, APPLAUNCH_CONTRACT first, APPLAUNCH_PDA=0 last of the contract's; a turned app's display variables
 * after them. */
Vars variables(const Input &in);

/* 'value' for sh -c (a ' inside becomes '\''). */
std::string shell_quote(const std::string &value);

/* "export K='v' ...; " for the front of the app's sh -c command line. */
std::string export_command(const Input &in);

/* From the layout service's numbers: compat = the compat window (m->compat) drawing the 320x170 canvas at its scale;
 * fullscreen = the whole logical screen at scale 1. state_dir = <runtime_dir>/applaunch when runtime_dir is set.
 * false (out untouched) without metrics. */
bool from_metrics(const cp0_ui_metrics_t *m, Mode mode, const char *runtime_dir, Input *out);

/* The clockwise pre-rotation of a turned app's landscape canvas: a quarter turn back from the launcher's portrait one
 * (90 -> 0, the DSI panel's own landscape; 270 -> 180; 0 -> 270; 180 -> 90), so the device is always turned the same
 * way. Anything else -> 0. */
int turned_rotation(int launcher_rot);

/* from_metrics(Fullscreen) for a landscape app on a portrait launcher (m->h > m->w), run turned: window and canvas the
 * panel's landscape canvas m->h x m->w at scale 1, plus its rotation (turned_rotation) and class. false (out untouched)
 * without metrics or when the canvas is not portrait. */
bool turned_from_metrics(const cp0_ui_metrics_t *m, const char *runtime_dir, Input *out);

/* export_command() for the running launcher (fullscreen: cp0_ui_metrics_panel(), the whole panel; compat:
 * cp0_ui_metrics_current(), the window as drawn; XDG_RUNTIME_DIR); "" when there are no
 * metrics (a build without the dpi-scaled backend, the SDL simulator): no contract there, the command stays as it was.
 * turned (fullscreen, desktop_fit() said Turned): turned_from_metrics(); on a canvas that is not portrait the plain
 * fullscreen variables. */
std::string export_for_current(Mode mode, bool turned = false);

} // namespace app_launch_env
