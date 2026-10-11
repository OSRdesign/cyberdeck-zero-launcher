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

std::string export_for_current(Mode mode)
{
    Input in;
    if (!from_metrics(cp0_ui_metrics_current(), mode, std::getenv("XDG_RUNTIME_DIR"), &in)) return std::string();
    return export_command(in);
}

} // namespace app_launch_env
