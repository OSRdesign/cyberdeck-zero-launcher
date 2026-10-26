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
static_assert(kAuto == CP0_UI_OSK_SETTING_AUTO && kOn == CP0_UI_OSK_SETTING_ON && kHand == CP0_UI_OSK_SETTING_HAND,
              "the stored values are the core's (cp0_ui_osk_setting_t)");

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

/* the PDA profile (APPLAUNCH_PDA=1 on the portrait panel: the writing zone exists) offers Handwriting + keyboard and
 * starts with it */
bool hand_offered()
{
    return launcher_zone::active();
}

bool offered(int value)
{
    return value == kAuto || value == kOn || (value == kHand && hand_offered());
}

/* input.state vkb=: the rule in force for apps (the app contract), the same inputs as allowed(); zone while the PDA
 * writing zone's band shows (report 042: the launcher is in front and text input is wanted) */
void publish_vkb()
{
    input_presence::set_vkb(launcher_zone::shown() && !launcher_zone::app_front()
                                ? CP0_APP_VKB_ZONE
                                : cp0_ui_osk_effective_vkb(s_env, mode(), !native_ui::enabled()));
}

bool allowed()
{
    cp0_ui_osk_policy_t policy{};
    policy.env = s_env;
    policy.setting_on = mode() == kOn; /* Handwriting + keyboard: the Auto rule, the band or the keyboard */
    policy.locked = ui_screensaver_is_active() != 0;
    policy.desktop_session = !native_ui::enabled(); /* the launcher draws the screen itself only with its backend */
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
    const int fallback = cp0_ui_osk_setting_default(hand_offered() ? 1 : 0);
    int value = fallback;
    if (call_config({"GetInt", kConfigKey, std::to_string(fallback)}, &text)) {
        int parsed = fallback;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        /* the old Off (until report 042) reads as Auto; it is written again only when the user picks a value */
        if (result.ec == std::errc{}) value = cp0_ui_osk_setting_read(parsed, hand_offered() ? 1 : 0);
    }
    s_mode = value;
    return s_mode;
}

int choices(int out[3])
{
    int n = 0;
    out[n++] = kAuto;
    out[n++] = kOn;
    if (hand_offered()) out[n++] = kHand;
    return n;
}

bool set_mode(int value)
{
    if (!offered(value)) return false;
    if (!call_config({"SetInt", kConfigKey, std::to_string(value)}) || !call_config({"Save"})) return false;
    s_mode = value;
    if (s_installed) {
        launcher_zone::mode_changed(); /* the band, its Keyb key and the keyboard's cross follow (report 042) */
        publish_vkb();
    }
    return true;
}

void install()
{
    if (s_installed) return;
    s_installed = true;
    s_env = cp0_ui_osk_env_parse(std::getenv("APPLAUNCH_OSK"));
    cp0::ui::osk::set_policy(allowed);
    launcher_zone::bind(); /* the PDA writing zone shows where the keyboard would (report 042) */
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
