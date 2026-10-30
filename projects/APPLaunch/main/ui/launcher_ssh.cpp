/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's SSH indicator setting and the feed's last sample (see launcher_ssh.hpp).
 */

#include "launcher_ssh.hpp"

#include "hal_lvgl_bsp.h"
#include "sample_log.h"

#include <atomic>
#include <cstdint>
#include <list>
#include <string>
#include <utility>

namespace launcher_ssh {
namespace {

int s_mode = -1; /* -1: not read yet */
bool s_installed = false;
std::atomic<int> s_enabled{0};          /* the feed thread's view: 1 once installed and On */
std::atomic<uint32_t> s_sample{0};      /* count << 1 | active: one word, never torn */

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

void apply()
{
    s_enabled.store(mode() == ssh_indicator::kOn ? 1 : 0);
    SLOGI("[SSH] Settings > SSH indicator: %s", mode() == ssh_indicator::kOn ? "On" : "Off");
}

} // namespace

int mode()
{
    if (s_mode >= 0) return s_mode;
    std::string text;
    int value = ssh_indicator::kOn;
    if (call_config({"GetInt", ssh_indicator::kConfigKey, std::to_string(ssh_indicator::kOn)}, &text))
        value = ssh_indicator::parse_mode(text);
    s_mode = value;
    return s_mode;
}

bool set_mode(int value)
{
    if (value != ssh_indicator::kOff && value != ssh_indicator::kOn) return false;
    if (value == mode()) return true; /* nothing to write: the key stays absent while it is the default */
    if (!call_config({"SetManyAndSave", ssh_indicator::kConfigKey, std::to_string(value)})) {
        SLOGW("[SSH] Settings > SSH indicator: the choice could not be saved");
        return false;
    }
    s_mode = value;
    if (s_installed) apply();
    return true;
}

void install()
{
    if (s_installed) return;
    s_installed = true;
    apply();
}

bool enabled()
{
    return s_enabled.load() != 0;
}

void publish(const ssh_indicator::Snapshot &sample)
{
    const uint32_t count = sample.count > 0 ? static_cast<uint32_t>(sample.count) : 0u;
    s_sample.store(count << 1 | (count > 0 && sample.active ? 1u : 0u));
}

ssh_indicator::Snapshot snapshot()
{
    const uint32_t word = s_sample.load();
    ssh_indicator::Snapshot out;
    out.count = static_cast<int>(word >> 1);
    out.active = static_cast<int>(word & 1u);
    return out;
}

} // namespace launcher_ssh
