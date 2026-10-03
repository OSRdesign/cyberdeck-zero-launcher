/*
 * Per-app touch gesture choice for the Raspberry Pi Zero 2W port (Settings > Touch).
 */

#include "touch_settings.hpp"

#include "hal_lvgl_bsp.h"

#include <cctype>
#include <charconv>
#include <list>
#include <string>
#include <utility>

namespace touch_settings {
namespace {

std::string config_key(const std::string &app_name)
{
    std::string key = "touch_";
    for (const unsigned char ch : app_name)
        if (std::isalnum(ch)) key.push_back(static_cast<char>(std::tolower(ch)));
    return key;
}

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

} // namespace

int default_choice(const std::string &app_name)
{
    if (app_name == "Tank") return kSwipeFire;
    if (app_name == "Snake" || app_name == "2048") return kSwipe;
    return kOff;
}

int choice(const std::string &app_name)
{
    std::string text;
    if (!call_config({"GetInt", config_key(app_name), "-1"}, &text)) return default_choice(app_name);
    int value = -1;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || value < kOff || value > kSwipeFire) return default_choice(app_name);
    return value;
}

bool set_choice(const std::string &app_name, int value)
{
    if (value < kOff || value > kSwipeFire) return false;
    const std::string key = config_key(app_name);
    return call_config({"SetInt", key, std::to_string(value)}) && call_config({"Save"});
}

} // namespace touch_settings
