/*
 * SPDX-License-Identifier: MIT
 *
 * Quick controls (the built-in "Quick controls" tile): four big buttons, Shutdown, Reboot, Wi-Fi and Bluetooth.
 * Pure logic, no LVGL: unit-tested on the PC by tests/test_quick_controls_model.cpp.
 *
 *   - what a button shows (label, state text) and what a tap does (decide());
 *   - how the state replies of the launcher's services are read (parse_*), and which adapters the board has
 *     (probe_adapters: sysfs, so nothing is started to find out);
 *   - the Controller: the ONE place that runs an action. Opening the page (refresh) and moving the focus only READ;
 *     only activate() switches a radio or starts a shutdown / reboot. The blocking service calls (Backend) run on a
 *     worker thread, their results come back through poll() on the UI thread.
 *
 * The Backend of the launcher (quick_controls_backend.cpp) reuses the services Settings already uses: the Wi-Fi radio
 * (cp0_signal_wifi_api RadioEnabled / RadioSetEnabled: nmcli radio), Bluetooth power (cp0_signal_bt_api BtStatus /
 * BtPower: BlueZ over D-Bus) and the process API's Shutdown / Reboot (systemctl poweroff -i / reboot -i, no password).
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace quick_controls {

enum class Control { Shutdown = 0, Reboot, Wifi, Bluetooth };
constexpr int kControlCount = 4;

/* what the page knows about a radio */
enum class Radio { Unknown, Unavailable, Off, On };

/* what a tap on a button starts */
enum class ActionKind { None, Shutdown, Reboot, WifiOn, WifiOff, BluetoothOn, BluetoothOff, Refresh };

struct Adapters {
    bool wifi = false;
    bool bluetooth = false;
};

struct State {
    Radio wifi = Radio::Unknown;
    Radio bluetooth = Radio::Unknown;
    bool wifi_busy = true;       /* checking the state (at open) or switching */
    bool bluetooth_busy = true;
    bool wifi_switching = false; /* busy because of a switch (not the first read) */
    bool bluetooth_switching = false;
    bool power_started = false;  /* a shutdown or reboot was started: the page is done */
    Control power = Control::Shutdown; /* which one, when power_started */
};

/* The blocking services (called from a worker thread only). */
struct Backend {
    std::function<Adapters()> adapters;
    std::function<Radio(Control)> read;           /* Wifi or Bluetooth: On, Off or Unknown */
    std::function<bool(Control, bool)> set;       /* Wifi or Bluetooth on / off: true when the service accepted it */
    std::function<bool(Control)> power;           /* Shutdown or Reboot: true when the command was started */
};

const char *label(Control control);
/* The text under the label: "On", "Off", "Not available", "Checking...", "Switching...", "Unknown", or for the power
 * buttons "Power off now" / "Restart now" ("Shutting down..." / "Restarting..." once started). */
std::string state_text(Control control, const State &state);
/* True when the button is drawn live (not dimmed): the same rule decide() acts on. */
bool active(Control control, const State &state);
/* What a tap / Enter on the button starts (None while it cannot: unavailable, busy, or after a power action). */
ActionKind decide(Control control, const State &state);

/* "RadioEnabled" reply: code 1 on, 0 off, anything else unknown. */
Radio parse_wifi_reply(int code);
/* "BtStatus" reply: code 0 and "powered<TAB>address<TAB>discoverable<TAB>alias" with powered 0 / 1. */
Radio parse_bluetooth_status(int code, const std::string &data);

/* The board's adapters from <root>/class/net/<if>/wireless (or phy80211), <root>/class/ieee80211/ (any entry), and
 * <root>/class/bluetooth/hci*; root is "/sys" on a board. */
Adapters probe_adapters(const std::string &root);
/* The test hook APPLAUNCH_QUICKCTL_ADAPTERS: "none", "wifi", "bluetooth" (or "bt"), "wifi,bluetooth" / "all"; NULL or
 * empty: not set (returns false and leaves out). */
bool parse_adapters_hook(const char *text, Adapters &out);
/* The hook if set, else probe_adapters("/sys"). */
Adapters detect_adapters();

/* Focus on the 2 x 2 (or 1 x 4) grid, row-major: arrows move one cell, no wrap, a move into nothing stays. */
enum class Move { Up, Down, Left, Right };
int move_focus(int index, int cols, int count, Move move);

/* Runs the actions. All state changes happen in poll(), on the caller's thread. */
class Controller {
public:
    using Spawn = std::function<void(std::function<void()>)>;
    static Spawn thread_spawn(); /* detached std::thread */

    explicit Controller(Backend backend, Spawn spawn = thread_spawn());
    ~Controller();
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;

    /* Page open: finds the adapters and reads the radios. Reads only; never switches anything. */
    void refresh();
    /* A tap / Enter: starts what decide() says and returns it (None: nothing started). */
    ActionKind activate(Control control);
    /* Applies the results that came in; true when the state or the message changed. */
    bool poll();
    bool pending() const;

    const State &state() const { return state_; }
    /* The line for the page's status caption ("" none) and whether it is an error. */
    const std::string &message() const { return message_; }
    bool message_error() const { return message_error_; }

private:
    struct Shared;
    void start_read_all();
    void start_switch(Control control, bool on);
    void start_power(Control control);

    Backend backend_;
    Spawn spawn_;
    std::shared_ptr<Shared> shared_;
    State state_;
    std::string message_;
    bool message_error_ = false;
};

} // namespace quick_controls
