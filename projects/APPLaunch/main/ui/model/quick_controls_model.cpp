/*
 * SPDX-License-Identifier: MIT
 *
 * Quick controls: the pure logic and the Controller (see quick_controls_model.hpp).
 */

#include "quick_controls_model.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace quick_controls {

namespace {

bool starts_with(const std::string &text, const char *prefix)
{
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}

bool has_entry(const std::filesystem::path &dir, bool (*match)(const std::filesystem::directory_entry &))
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (match(*it)) return true;
    return false;
}

} // namespace

const char *label(Control control)
{
    switch (control) {
    case Control::Shutdown: return "Shutdown";
    case Control::Reboot: return "Reboot";
    case Control::Wifi: return "Wi-Fi";
    case Control::Bluetooth: return "Bluetooth";
    }
    return "";
}

std::string state_text(Control control, const State &state)
{
    if (control == Control::Shutdown)
        return state.power_started && state.power == Control::Shutdown ? "Shutting down..." : "Power off now";
    if (control == Control::Reboot)
        return state.power_started && state.power == Control::Reboot ? "Restarting..." : "Restart now";
    const bool wifi = control == Control::Wifi;
    const Radio radio = wifi ? state.wifi : state.bluetooth;
    const bool busy = wifi ? state.wifi_busy : state.bluetooth_busy;
    const bool switching = wifi ? state.wifi_switching : state.bluetooth_switching;
    if (busy) return switching ? "Switching..." : "Checking...";
    switch (radio) {
    case Radio::On: return "On";
    case Radio::Off: return "Off";
    case Radio::Unavailable: return "Not available";
    case Radio::Unknown: return "Unknown";
    }
    return "";
}

ActionKind decide(Control control, const State &state)
{
    if (state.power_started) return ActionKind::None; /* the machine is going down: nothing else acts */
    switch (control) {
    case Control::Shutdown: return ActionKind::Shutdown;
    case Control::Reboot: return ActionKind::Reboot;
    case Control::Wifi:
    case Control::Bluetooth: {
        const bool wifi = control == Control::Wifi;
        if (wifi ? state.wifi_busy : state.bluetooth_busy) return ActionKind::None;
        switch (wifi ? state.wifi : state.bluetooth) {
        case Radio::Unavailable: return ActionKind::None;
        case Radio::Unknown: return ActionKind::Refresh; /* the read failed: a tap reads again */
        case Radio::On: return wifi ? ActionKind::WifiOff : ActionKind::BluetoothOff;
        case Radio::Off: return wifi ? ActionKind::WifiOn : ActionKind::BluetoothOn;
        }
        break;
    }
    }
    return ActionKind::None;
}

bool active(Control control, const State &state)
{
    return decide(control, state) != ActionKind::None;
}

Radio parse_wifi_reply(int code)
{
    return code == 1 ? Radio::On : code == 0 ? Radio::Off : Radio::Unknown;
}

Radio parse_bluetooth_status(int code, const std::string &data)
{
    if (code != 0 || data.empty()) return Radio::Unknown;
    const std::size_t tab = data.find('\t');
    const std::string powered = data.substr(0, tab);
    if (powered == "1") return Radio::On;
    if (powered == "0") return Radio::Off;
    return Radio::Unknown;
}

Adapters probe_adapters(const std::string &root)
{
    namespace fs = std::filesystem;
    Adapters found;
    const fs::path base(root);
    std::error_code ec;
    // a wireless interface: <if>/wireless (wext) or <if>/phy80211 (cfg80211); a bare phy under ieee80211 counts too
    for (fs::directory_iterator it(base / "class" / "net", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code probe;
        if (fs::exists(it->path() / "wireless", probe) || fs::exists(it->path() / "phy80211", probe)) {
            found.wifi = true;
            break;
        }
    }
    if (!found.wifi) found.wifi = has_entry(base / "class" / "ieee80211", [](const fs::directory_entry &) { return true; });
    found.bluetooth = has_entry(base / "class" / "bluetooth", [](const fs::directory_entry &entry) {
        return starts_with(entry.path().filename().string(), "hci");
    });
    return found;
}

bool parse_adapters_hook(const char *text, Adapters &out)
{
    if (!text || !text[0]) return false;
    out = Adapters{};
    std::string rest(text);
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string word = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        if (word == "wifi") out.wifi = true;
        else if (word == "bluetooth" || word == "bt") out.bluetooth = true;
        else if (word == "all") out.wifi = out.bluetooth = true;
    }
    return true;
}

Adapters detect_adapters()
{
    Adapters hooked;
    if (parse_adapters_hook(std::getenv("APPLAUNCH_QUICKCTL_ADAPTERS"), hooked)) return hooked;
    return probe_adapters("/sys");
}

int move_focus(int index, int cols, int count, Move move)
{
    if (cols < 1 || count < 1 || index < 0 || index >= count) return index < 0 ? 0 : index;
    const int row = index / cols;
    const int col = index % cols;
    switch (move) {
    case Move::Left: return col > 0 ? index - 1 : index;
    case Move::Right: return col < cols - 1 && index + 1 < count ? index + 1 : index;
    case Move::Up: return row > 0 ? index - cols : index;
    case Move::Down: return index + cols < count ? index + cols : index;
    }
    return index;
}

/* ---------------------------------------------------------------- Controller */

struct Controller::Shared {
    enum class Kind { Read, Switch, Power };
    struct Result {
        Kind kind = Kind::Read;
        Control control = Control::Wifi;
        Radio wifi = Radio::Unknown;
        Radio bluetooth = Radio::Unknown;
        bool ok = false;
        bool on = false;
    };
    std::mutex mutex;
    std::vector<Result> done;
    bool power_inflight = false;

    void push(Result result)
    {
        std::lock_guard<std::mutex> lock(mutex);
        done.push_back(result);
    }
};

Controller::Spawn Controller::thread_spawn()
{
    return [](std::function<void()> job) {
        try {
            std::thread(std::move(job)).detach();
        } catch (...) {
        }
    };
}

Controller::Controller(Backend backend, Spawn spawn)
    : backend_(std::move(backend)), spawn_(std::move(spawn)), shared_(std::make_shared<Shared>())
{
}

Controller::~Controller() = default; /* a worker that finishes later only fills the shared list, which it keeps alive */

void Controller::refresh()
{
    start_read_all();
}

void Controller::start_read_all()
{
    state_.wifi_busy = state_.bluetooth_busy = true;
    state_.wifi_switching = state_.bluetooth_switching = false;
    auto shared = shared_;
    Backend backend = backend_;
    spawn_([shared, backend] {
        Shared::Result result;
        result.kind = Shared::Kind::Read;
        Adapters adapters;
        if (backend.adapters) adapters = backend.adapters();
        result.wifi = !adapters.wifi ? Radio::Unavailable : backend.read ? backend.read(Control::Wifi) : Radio::Unknown;
        result.bluetooth =
            !adapters.bluetooth ? Radio::Unavailable : backend.read ? backend.read(Control::Bluetooth) : Radio::Unknown;
        shared->push(result);
    });
}

void Controller::start_switch(Control control, bool on)
{
    const bool wifi = control == Control::Wifi;
    (wifi ? state_.wifi_busy : state_.bluetooth_busy) = true;
    (wifi ? state_.wifi_switching : state_.bluetooth_switching) = true;
    message_.clear();
    message_error_ = false;
    auto shared = shared_;
    Backend backend = backend_;
    spawn_([shared, backend, control, on] {
        Shared::Result result;
        result.kind = Shared::Kind::Switch;
        result.control = control;
        result.on = on;
        result.ok = backend.set ? backend.set(control, on) : false;
        const Radio after = backend.read ? backend.read(control) : Radio::Unknown;
        (control == Control::Wifi ? result.wifi : result.bluetooth) = after;
        shared->push(result);
    });
}

void Controller::start_power(Control control)
{
    state_.power_started = true;
    state_.power = control;
    message_ = control == Control::Shutdown ? "Shutting down..." : "Restarting...";
    message_error_ = false;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        shared_->power_inflight = true;
    }
    auto shared = shared_;
    Backend backend = backend_;
    spawn_([shared, backend, control] {
        Shared::Result result;
        result.kind = Shared::Kind::Power;
        result.control = control;
        result.ok = backend.power ? backend.power(control) : false;
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->power_inflight = false;
        shared->done.push_back(result);
    });
}

ActionKind Controller::activate(Control control)
{
    const ActionKind action = decide(control, state_);
    switch (action) {
    case ActionKind::None: break;
    case ActionKind::Shutdown: start_power(Control::Shutdown); break;
    case ActionKind::Reboot: start_power(Control::Reboot); break;
    case ActionKind::WifiOn: start_switch(Control::Wifi, true); break;
    case ActionKind::WifiOff: start_switch(Control::Wifi, false); break;
    case ActionKind::BluetoothOn: start_switch(Control::Bluetooth, true); break;
    case ActionKind::BluetoothOff: start_switch(Control::Bluetooth, false); break;
    case ActionKind::Refresh: start_read_all(); break;
    }
    return action;
}

bool Controller::poll()
{
    std::vector<Shared::Result> results;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        results.swap(shared_->done);
    }
    bool changed = false;
    for (const Shared::Result &result : results) {
        changed = true;
        switch (result.kind) {
        case Shared::Kind::Read:
            state_.wifi = result.wifi;
            state_.bluetooth = result.bluetooth;
            state_.wifi_busy = state_.bluetooth_busy = false;
            state_.wifi_switching = state_.bluetooth_switching = false;
            break;
        case Shared::Kind::Switch: {
            const bool wifi = result.control == Control::Wifi;
            Radio after = wifi ? result.wifi : result.bluetooth;
            if (after == Radio::Unknown && result.ok) after = result.on ? Radio::On : Radio::Off;
            (wifi ? state_.wifi : state_.bluetooth) = after;
            (wifi ? state_.wifi_busy : state_.bluetooth_busy) = false;
            (wifi ? state_.wifi_switching : state_.bluetooth_switching) = false;
            message_error_ = !result.ok;
            message_ = result.ok ? std::string() : std::string(label(result.control)) + " did not switch";
            break;
        }
        case Shared::Kind::Power:
            if (!result.ok) {
                state_.power_started = false;
                message_error_ = true;
                message_ = result.control == Control::Shutdown ? "Shutdown failed" : "Reboot failed";
            }
            break;
        }
    }
    return changed;
}

bool Controller::pending() const
{
    bool inflight = false;
    {
        std::lock_guard<std::mutex> lock(shared_->mutex);
        inflight = shared_->power_inflight || !shared_->done.empty();
    }
    return inflight || state_.wifi_busy || state_.bluetooth_busy;
}

} // namespace quick_controls
