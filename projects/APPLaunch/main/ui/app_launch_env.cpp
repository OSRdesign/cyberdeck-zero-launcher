/*
 * SPDX-License-Identifier: MIT
 *
 * The per-launch environment of the app contract (see app_launch_env.hpp).
 */
#include "app_launch_env.hpp"

#include <cstdlib>

namespace app_launch_env {

Vars variables(const Input &in)
{
    Vars vars;
    vars.emplace_back("APPLAUNCH_CONTRACT", "1");
    vars.emplace_back("APPLAUNCH_APP_MODE", in.mode == Mode::Fullscreen ? "fullscreen" : "compat");
    vars.emplace_back("APPLAUNCH_WINDOW", std::to_string(in.win_x) + "," + std::to_string(in.win_y) + "," +
                                              std::to_string(in.win_w) + "," + std::to_string(in.win_h));
    vars.emplace_back("APPLAUNCH_CANVAS", std::to_string(in.canvas_w) + "x" + std::to_string(in.canvas_h));
    vars.emplace_back("APPLAUNCH_CANVAS_SCALE", std::to_string(in.canvas_scale));
    if (!in.state_dir.empty()) vars.emplace_back("APPLAUNCH_STATE_DIR", in.state_dir);
    if (in.turned && in.mode == Mode::Fullscreen) {
        /* the app's own cp0_lvgl backend reads APPLAUNCH_ROTATE / APPLAUNCH_LOGICAL (board profile names); an app with
         * its own glue (viz1090) reads APPLAUNCH_SCREEN_* first: both must describe the landscape canvas */
        const std::string rot = std::to_string(in.turned_rotate);
        const std::string w = std::to_string(in.canvas_w), h = std::to_string(in.canvas_h);
        vars.emplace_back("APPLAUNCH_ROTATE", rot);
        vars.emplace_back("APPLAUNCH_LOGICAL", w + "x" + h);
        vars.emplace_back("APPLAUNCH_SCREEN_W", w);
        vars.emplace_back("APPLAUNCH_SCREEN_H", h);
        vars.emplace_back("APPLAUNCH_SCREEN_ROTATE", rot);
        vars.emplace_back("APPLAUNCH_SCREEN_CLASS", in.turned_class);
    }
    return vars;
}

std::string shell_quote(const std::string &value)
{
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    return quoted;
}

std::string export_command(const Input &in)
{
    std::string command = "export";
    for (const auto &kv : variables(in)) command += " " + kv.first + "=" + shell_quote(kv.second);
    return command + "; ";
}

bool from_metrics(const cp0_ui_metrics_t *m, Mode mode, const char *runtime_dir, Input *out)
{
    if (!m || !out || m->w <= 0 || m->h <= 0) return false;
    Input in;
    in.mode = mode;
    if (mode == Mode::Compat) {
        in.win_x = m->compat.x;
        in.win_y = m->compat.y;
        in.win_w = m->compat.w;
        in.win_h = m->compat.h;
        in.canvas_w = CP0_UI_COMPAT_W;
        in.canvas_h = CP0_UI_COMPAT_H;
        in.canvas_scale = m->compat.scale > 0 ? m->compat.scale : 1;
    } else {
        in.win_w = m->w;
        in.win_h = m->h;
        in.canvas_w = m->w;
        in.canvas_h = m->h;
        in.canvas_scale = 1;
    }
    if (runtime_dir && runtime_dir[0]) in.state_dir = std::string(runtime_dir) + "/" CP0_UI_STATE_SUBDIR;
    *out = in;
    return true;
}

int turned_rotation(int launcher_rot)
{
    switch (launcher_rot) {
    case 0: return 270;
    case 90: return 0;
    case 180: return 90;
    case 270: return 180;
    default: return 0;
    }
}

bool turned_from_metrics(const cp0_ui_metrics_t *m, const char *runtime_dir, Input *out)
{
    if (!m || !out || m->w <= 0 || m->h <= m->w) return false;
    Input in;
    if (!from_metrics(m, Mode::Fullscreen, runtime_dir, &in)) return false;
    in.win_w = in.canvas_w = m->h;
    in.win_h = in.canvas_h = m->w;
    in.turned = true;
    in.turned_rotate = turned_rotation(m->rot);
    in.turned_class = cp0_ui_class_name(cp0_ui_classify(m->h, m->w));
    *out = in;
    return true;
}

std::string export_for_current(Mode mode, bool turned)
{
    const cp0_ui_metrics_t *m = cp0_ui_metrics_current();
    const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    Input in;
    if (turned && mode == Mode::Fullscreen && turned_from_metrics(m, runtime_dir, &in)) return export_command(in);
    if (!from_metrics(m, mode, runtime_dir, &in)) return std::string();
    return export_command(in);
}

} // namespace app_launch_env
