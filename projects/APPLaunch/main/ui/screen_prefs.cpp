/*
 * SPDX-License-Identifier: MIT
 *
 * Storage and start-up effect of the Settings > Screen preferences (see screen_prefs.hpp).
 */

#include "screen_prefs.hpp"

#include "hal_lvgl_bsp.h"
#include "sample_log.h"

#include <cstdio>
#include <cstdlib>
#include <list>
#include <utility>

namespace screen_prefs {
namespace {

bool s_ready = false;
Started s_started;

bool call_config(std::list<std::string> arguments, std::string *data = nullptr)
{
    bool ok = false;
    try {
        cp0_signal_config_api(std::move(arguments), [&](int code, std::string reply) {
            ok = code == 0;
            if (data) *data = std::move(reply);
        });
    } catch (...) {
        return false;
    }
    return ok;
}

std::string read_file(const std::string &path)
{
    FILE *fp = std::fopen(path.c_str(), "r");
    if (!fp) return {};
    std::string text;
    char buffer[512];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), fp)) > 0 && text.size() < (1u << 20))
        text.append(buffer, count);
    std::fclose(fp);
    return text;
}

std::string config_path()
{
    const char *home = std::getenv("HOME");
    return std::string(home && home[0] ? home : "/root") + "/.config/cardputerzero/config.json";
}

} // namespace

const Started &apply_at_start()
{
    if (s_ready) return s_started;
    s_ready = true;
    Started started;
    started.stored = stored_from_json(read_file(config_path()));
    if (const char *board = std::getenv("APPLAUNCH_PANEL_MM")) started.board_mm = board;
    const std::string mm = panel_mm_to_apply(started.stored.mm);
    if (!mm.empty()) {
        ::setenv("APPLAUNCH_PANEL_MM", mm.c_str(), 1);
        started.applied = true;
    }
    std::fprintf(stderr, "[screen-prefs] idle dim %s, terminal font %s, panel size %s (board.conf %s)\n",
                 started.stored.dim ? std::to_string(started.stored.dim).c_str() : "default",
                 started.stored.terminal ? std::to_string(started.stored.terminal).c_str() : "auto",
                 started.applied ? mm.c_str() : "not set by the setting",
                 started.board_mm.empty() ? "none" : started.board_mm.c_str());
    s_started = started;
    return s_started;
}

const Started &startup()
{
    return s_started;
}

Stored stored()
{
    Stored s;
    std::string text;
    if (call_config({"GetInt", kDimKey, "0"}, &text)) s.dim = stored_int(text, 1, 100);
    if (call_config({"GetInt", kTermKey, "0"}, &text)) s.terminal = stored_int(text, 10, 48);
    std::string diag, mm;
    if (call_config({"GetStr", kDiagKey, ""}, &text)) diag = text;
    if (call_config({"GetStr", kMmKey, ""}, &text)) mm = text;
    if (diag_valid(diag) && mm_valid(mm)) {
        s.diag = diag;
        s.mm = mm;
    }
    return s;
}

int stored_terminal_font()
{
    std::string text;
    return call_config({"GetInt", kTermKey, "0"}, &text) ? stored_int(text, 10, 48) : 0;
}

bool save_dim(int percent)
{
    if (percent != 0 && !dim_stored_valid(percent)) return false;
    if (percent == stored().dim) return true; /* nothing to write: the key stays absent while it is the default */
    if (!call_config({"SetManyAndSave", kDimKey, std::to_string(percent)})) {
        SLOGW("[screen-prefs] the idle dim level could not be saved");
        return false;
    }
    return true;
}

bool save_terminal_font(int px)
{
    if (px != 0 && (px < 10 || px > 48)) return false;
    if (px == stored_terminal_font()) return true;
    if (!call_config({"SetManyAndSave", kTermKey, std::to_string(px)})) {
        SLOGW("[screen-prefs] the terminal text size could not be saved");
        return false;
    }
    return true;
}

bool save_size(const std::string &diag, const std::string &mm)
{
    if (!diag.empty() && (!diag_valid(diag) || !mm_valid(mm))) return false;
    const Stored now = stored();
    if (diag == now.diag && mm == now.mm) return true;
    if (!call_config({"SetManyAndSave", kDiagKey, diag, kMmKey, mm})) {
        SLOGW("[screen-prefs] the screen size could not be saved");
        return false;
    }
    return true;
}

std::string installer_size()
{
    const char *path = std::getenv("APPLAUNCH_INSTALL_CONF");
    return installer_size_from_conf(read_file(path && path[0] ? path : "/etc/applaunch/install.conf"));
}

SizeSource size_source_in_force()
{
    if (s_started.applied) return SizeSource::Setting;
    return installer_size().empty() ? SizeSource::Auto : SizeSource::Installer;
}

} // namespace screen_prefs
