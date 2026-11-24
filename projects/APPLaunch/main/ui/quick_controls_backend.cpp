/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's services behind the Quick controls buttons (see quick_controls_backend.hpp).
 */

#include "quick_controls_backend.hpp"

#include "cp0_lvgl_app.h"
#include "hal_lvgl_bsp.h"

#include <chrono>
#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <string>

namespace quick_controls {

namespace {

constexpr auto kReadTimeout = std::chrono::milliseconds(3000);
constexpr auto kSwitchTimeout = std::chrono::milliseconds(10000); // BlueZ may take a few seconds to power the adapter
constexpr auto kPowerTimeout = std::chrono::milliseconds(15000);

struct Reply {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false;
    int code = -1;
    std::string data;
};

/* One service call, waited for (the thread is ours). A reply may come at once, later, or from another thread, so the
 * callback owns the reply through a shared_ptr. */
template <class Signal>
bool call(Signal &signal, std::list<std::string> args, std::chrono::milliseconds timeout, int &code, std::string &data)
{
    auto reply = std::make_shared<Reply>();
    try {
        signal(std::move(args), [reply](int c, std::string d) {
            std::lock_guard<std::mutex> lock(reply->mutex);
            reply->code = c;
            reply->data = std::move(d);
            reply->done = true;
            reply->ready.notify_all();
        });
    } catch (...) {
        return false;
    }
    std::unique_lock<std::mutex> lock(reply->mutex);
    if (!reply->ready.wait_for(lock, timeout, [&] { return reply->done; })) return false;
    code = reply->code;
    data = reply->data;
    return true;
}

Radio read_radio(Control control)
{
    int code = -1;
    std::string data;
    if (control == Control::Wifi) {
        if (!call(cp0_signal_wifi_api, {"RadioEnabled"}, kReadTimeout, code, data)) return Radio::Unknown;
        return parse_wifi_reply(code);
    }
    if (!call(cp0_signal_bt_api, {"BtStatus"}, kReadTimeout, code, data)) return Radio::Unknown;
    return parse_bluetooth_status(code, data);
}

bool set_radio(Control control, bool on)
{
    int code = -1;
    std::string data;
    if (control == Control::Wifi)
        return call(cp0_signal_wifi_api, {"RadioSetEnabled", on ? "on" : "off"}, kSwitchTimeout, code, data) &&
               code == 0;
    return call(cp0_signal_bt_api, {"BtPower", on ? "1" : "0"}, kSwitchTimeout, code, data) && code == 0;
}

bool power(Control control)
{
    int code = -1;
    std::string data;
    return call(cp0_signal_process_api, {control == Control::Reboot ? "Reboot" : "Shutdown"}, kPowerTimeout, code,
                data) &&
           code == 0;
}

} // namespace

Backend launcher_backend()
{
    Backend backend;
    backend.adapters = [] { return detect_adapters(); };
    backend.read = read_radio;
    backend.set = set_radio;
    backend.power = power;
    return backend;
}

} // namespace quick_controls
