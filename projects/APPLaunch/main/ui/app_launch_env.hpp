/*
 * SPDX-License-Identifier: MIT
 *
 * The per-launch environment of the app contract (task 014 P5, report 023, cp0_app_contract.h): what the launcher
 * puts on the command line of every app it starts, so the app knows its mode, where its picture is on the screen,
 * the canvas it draws and where the state files are:
 *   APPLAUNCH_CONTRACT=1 APPLAUNCH_APP_MODE=compat|fullscreen APPLAUNCH_WINDOW=x,y,w,h APPLAUNCH_CANVAS=WxH
 *   APPLAUNCH_CANVAS_SCALE=N [APPLAUNCH_STATE_DIR=<dir>]
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
};

using Vars = std::vector<std::pair<std::string, std::string>>;

/* The variables, APPLAUNCH_CONTRACT first. */
Vars variables(const Input &in);

/* 'value' for sh -c (a ' inside becomes '\''). */
std::string shell_quote(const std::string &value);

/* "export K='v' ...; " for the front of the app's sh -c command line. */
std::string export_command(const Input &in);

/* From the layout service's numbers: compat = the compat window (m->compat) drawing the 320x170 canvas at its scale;
 * fullscreen = the whole logical screen at scale 1. state_dir = <runtime_dir>/applaunch when runtime_dir is set.
 * false (out untouched) without metrics. */
bool from_metrics(const cp0_ui_metrics_t *m, Mode mode, const char *runtime_dir, Input *out);

/* export_command() for the running launcher (cp0_ui_metrics_current(), XDG_RUNTIME_DIR); "" when there are no
 * metrics (the stock CardputerZero build, the SDL simulator): no contract there, the command stays as it was. */
std::string export_for_current(Mode mode);

} // namespace app_launch_env
