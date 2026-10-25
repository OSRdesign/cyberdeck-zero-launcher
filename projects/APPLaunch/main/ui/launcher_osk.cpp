/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's on-screen keyboard policy and setting (see launcher_osk.hpp).
 */

#include "launcher_osk.hpp"

#include "input_presence.hpp"
#include "launcher_zone.hpp"
#include "native_ui.hpp"
#include "ui_screensaver.h"

#include "cp0_ui_osk.h"
#include "cp0_ui_widgets.hpp"
#include "hal_lvgl_bsp.h"

#include <charconv>
#include <cstdlib>
#include <list>
#include <string>
#include <utility>

namespace launcher_osk {
namespace {

constexpr const char *kConfigKey = "osk_mode";

int s_mode = -1; /* -1: not read yet */
bool s_installed = false;
cp0_ui_osk_env_t s_env = CP0_UI_OSK_ENV_UNSET;

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

/* The PDA writing zone is the text input now (report 041): it exists and the launcher is in front (a fullscreen app in
 * front runs turned, without it, and keeps the usual rule). */
bool zone_input()
{
    return launcher_zone::active() && !launcher_zone::app_front();
}

/* input.state vkb=: the rule in force for apps (the app contract), the same inputs as allowed() */
void publish_vkb()
{
    input_presence::set_vkb(zone_input() ? CP0_APP_VKB_ZONE
                                         : cp0_ui_osk_effective_vkb(s_env, mode() == kOff, !native_ui::enabled()));
}

bool allowed()
{
    cp0_ui_osk_policy_t policy{};
    policy.env = s_env;
    policy.setting_off = mode() == kOff;
    policy.locked = ui_screensaver_is_active() != 0;
    policy.desktop_session = !native_ui::enabled(); /* the launcher draws the screen itself only with its backend */
    policy.zone = zone_input() ? 1 : 0;             /* the writing zone is the text input: never the keyboard */
    const input_presence::KeyboardSnapshot keyboards = input_presence::keyboards();
    policy.keyboards = keyboards.keyboards;
    policy.now_s = keyboards.now_s;
    policy.any_gone_s = keyboards.any_gone_s;
    policy.bt_gone_s = keyboards.bt_gone_s;
    return cp0_ui_osk_policy_allows(&policy) != 0;
}

} // namespace

int mode()
{
    if (s_mode >= 0) return s_mode;
    std::string text;
    int value = kAuto;
    if (call_config({"GetInt", kConfigKey, "0"}, &text)) {
        int parsed = kAuto;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec == std::errc{} && (parsed == kAuto || parsed == kOff)) value = parsed;
    }
    s_mode = value;
    return s_mode;
}

bool set_mode(int value)
{
    if (value != kAuto && value != kOff) return false;
    if (!call_config({"SetInt", kConfigKey, std::to_string(value)}) || !call_config({"Save"})) return false;
    s_mode = value;
    if (s_installed) publish_vkb();
    return true;
}

void install()
{
    if (s_installed) return;
    s_installed = true;
    s_env = cp0_ui_osk_env_parse(std::getenv("APPLAUNCH_OSK"));
    cp0::ui::osk::set_policy(allowed);
    publish_vkb();
}

void dismiss()
{
    cp0::ui::osk::dismiss();
}

void refresh()
{
    if (s_installed) publish_vkb();
}

} // namespace launcher_osk
