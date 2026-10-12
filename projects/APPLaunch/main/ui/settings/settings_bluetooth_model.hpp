/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Bluetooth model shared by the two Settings renderers (task 014 P3c): the legacy Paired Devices / Scan / Alias pages
// (settings_bluetooth_page.cpp) and the native Bluetooth pages (settings_native_bluetooth_page.cpp).
//
//   settings_bluetooth_com   the Bluetooth service's wire format as both renderers use it (cp0_signal_bt_api requests
//                            and the status / device records they answer with). Moved unchanged from
//                            settings_bluetooth_page.cpp.
//   settings_bluetooth::     pure rules (no LVGL, no service call, no thread; tests/test_settings_bluetooth_model.cpp):
//                            the Named Only filter, the agent prompt rules (which BlueZ request needs what, the input
//                            checks, the session-scoped authorization), the texts; and the native pages' state
//                            machines (DeviceListModel: status, list, discovery, pair / connect / disconnect / forget /
//                            rename, exactly the legacy page's protocol; AliasModel: the adapter alias). The state
//                            machines talk to the service through a Transport the page provides (a worker thread, the
//                            reply on the LVGL thread), so the tests drive them with a fake.
#pragma once

#include "cp0_lvgl_app.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

class settings_bluetooth_com {
public:
    struct StatusRecord {
        bool powered = false;
        std::string address;
        bool discoverable = false;
        std::string alias;
    };

    struct DeviceRecord {
        std::string address;
        int rssi = 0;
        bool connected = false;
        bool paired = false;
        bool trusted = false;
        std::string name;
    };

    enum class ValidationLimit : std::size_t {
        MaxAliasBytes     = CP0_BT_NAME_MAX - 1,
        MaxAddressBytes   = sizeof(((cp0_bt_status_t *)nullptr)->address) - 1,
        MaxDeviceNameBytes = CP0_BT_NAME_MAX - 1,
    };

    static bool parse_boolean(std::string_view value, bool &output)
    {
        if (value != "0" && value != "1") return false;
        output = value == "1";
        return true;
    }

    static bool parse_integer(std::string_view value, int &output)
    {
        if (value.empty()) return false;
        int parsed = 0;
        const char *begin = value.data();
        const char *end = begin + value.size();
        const auto result = std::from_chars(begin, end, parsed, 10);
        if (result.ec != std::errc{} || result.ptr != end) return false;
        output = parsed;
        return true;
    }

    static bool has_wire_control(std::string_view value)
    {
        for (const unsigned char byte : value) {
            if (byte < 0x20 || byte == 0x7f) return true;
        }
        return false;
    }

    static bool valid_utf8(std::string_view value)
    {
        std::size_t index = 0;
        while (index < value.size()) {
            const unsigned char first = static_cast<unsigned char>(value[index]);
            if (first <= 0x7f) {
                ++index;
                continue;
            }

            std::size_t width = 0;
            unsigned char second_min = 0x80;
            unsigned char second_max = 0xbf;
            if (first >= 0xc2 && first <= 0xdf) {
                width = 2;
            } else if (first == 0xe0) {
                width = 3;
                second_min = 0xa0;
            } else if (first >= 0xe1 && first <= 0xec) {
                width = 3;
            } else if (first == 0xed) {
                width = 3;
                second_max = 0x9f;
            } else if (first >= 0xee && first <= 0xef) {
                width = 3;
            } else if (first == 0xf0) {
                width = 4;
                second_min = 0x90;
            } else if (first >= 0xf1 && first <= 0xf3) {
                width = 4;
            } else if (first == 0xf4) {
                width = 4;
                second_max = 0x8f;
            } else {
                return false;
            }

            if (index + width > value.size()) return false;
            const unsigned char second = static_cast<unsigned char>(value[index + 1]);
            if (second < second_min || second > second_max) return false;
            for (std::size_t continuation = 2; continuation < width; ++continuation) {
                const unsigned char byte =
                    static_cast<unsigned char>(value[index + continuation]);
                if (byte < 0x80 || byte > 0xbf) return false;
            }
            index += width;
        }
        return true;
    }

    static bool valid_text_field(std::string_view value,
                                 std::size_t maximum_bytes,
                                 bool allow_empty = true)
    {
        if ((!allow_empty && value.empty()) || value.size() > maximum_bytes) return false;
        return !has_wire_control(value) && valid_utf8(value);
    }

    static bool valid_device_address(std::string_view address)
    {
        if (address.size() != 17) return false;
        for (std::size_t index = 0; index < address.size(); ++index) {
            if ((index + 1) % 3 == 0) {
                if (address[index] != ':') return false;
            } else if (!std::isxdigit(static_cast<unsigned char>(address[index]))) {
                return false;
            }
        }
        return true;
    }

    static bool split_fields(std::string_view record,
                             std::size_t expected_count,
                             std::vector<std::string_view> &fields)
    {
        fields.clear();
        std::size_t start = 0;
        while (true) {
            const std::size_t separator = record.find('\t', start);
            fields.emplace_back(record.substr(
                start,
                separator == std::string_view::npos ? std::string_view::npos : separator - start));
            if (separator == std::string_view::npos) break;
            start = separator + 1;
        }
        return fields.size() == expected_count;
    }

    static bool decode_status(std::string_view payload, StatusRecord &output)
    {
        std::vector<std::string_view> fields;
        if (!split_fields(payload, 4, fields)) return false;

        StatusRecord decoded;
        if (!parse_boolean(fields[0], decoded.powered) ||
            !parse_boolean(fields[2], decoded.discoverable))
            return false;
        if (fields[1].size() > static_cast<std::size_t>(ValidationLimit::MaxAddressBytes) ||
            has_wire_control(fields[1]))
            return false;
        if (decoded.powered && fields[1].empty()) return false;
        if (!fields[1].empty() && !valid_device_address(fields[1])) return false;
        if (!valid_text_field(fields[3], static_cast<std::size_t>(ValidationLimit::MaxAliasBytes)))
            return false;
        decoded.address.assign(fields[1]);
        decoded.alias.assign(fields[3]);
        output = std::move(decoded);
        return true;
    }

    static bool decode_device(std::string_view payload, DeviceRecord &output)
    {
        std::vector<std::string_view> fields;
        if (!split_fields(payload, 6, fields)) return false;
        DeviceRecord decoded;
        if (!valid_device_address(fields[0]) || !parse_integer(fields[1], decoded.rssi) ||
            !parse_boolean(fields[2], decoded.connected) ||
            !parse_boolean(fields[3], decoded.paired) ||
            !parse_boolean(fields[4], decoded.trusted) ||
            !valid_text_field(fields[5], static_cast<std::size_t>(ValidationLimit::MaxDeviceNameBytes)))
            return false;
        decoded.address.assign(fields[0]);
        decoded.name.assign(fields[5]);
        output = std::move(decoded);
        return true;
    }

    static bool decode_devices(std::string_view payload,
                                std::vector<DeviceRecord> &output,
                                int maximum_count = CP0_BT_DEVICE_MAX)
    {
        output.clear();
        if (maximum_count < 0 || maximum_count > CP0_BT_DEVICE_MAX) return false;
        if (payload.empty()) return true;
        if (payload.back() == '\n') {
            payload.remove_suffix(1);
            if (payload.empty() || payload.back() == '\n') return false;
        }
        std::size_t start = 0;
        while (start < payload.size()) {
            const std::size_t separator = payload.find('\n', start);
            const std::size_t end = separator == std::string_view::npos ? payload.size() : separator;
            const std::string_view line = payload.substr(start, end - start);
            if (line.empty() || static_cast<int>(output.size()) >= maximum_count) return false;
            DeviceRecord device;
            if (!decode_device(line, device)) return false;
            output.push_back(std::move(device));
            if (separator == std::string_view::npos) break;
            start = separator + 1;
        }
        return true;
    }

    static bool decode_device_list_reply(int code,
                                         std::string_view payload,
                                         std::vector<DeviceRecord> &output,
                                         int maximum_count = CP0_BT_DEVICE_MAX)
    {
        output.clear();
        if (code < 0 || code > maximum_count) return false;
        if (!decode_devices(payload, output, maximum_count)) return false;
        return static_cast<int>(output.size()) == code;
    }

    static bool valid_alias(std::string_view alias)
    {
        return valid_text_field(alias, static_cast<std::size_t>(ValidationLimit::MaxAliasBytes), false);
    }

    static bool success_without_payload(int code, std::string_view payload)
    {
        return code == 0 && (payload.empty() || payload == "ok");
    }

    static std::list<std::string> status_request() { return {"BtStatus"}; }

    static std::list<std::string> power_request(bool enabled)
    {
        return {"BtPower", enabled ? "1" : "0"};
    }

    static std::list<std::string> discoverable_request(bool enabled)
    {
        return {"BtDiscoverable", enabled ? "1" : "0"};
    }

    static bool alias_request(std::string_view alias, std::list<std::string> &request)
    {
        if (!valid_alias(alias)) return false;
        request = {"BtAlias", std::string(alias)};
        return true;
    }

    /* task 014 P3c: a paired device's name (BlueZ Device1.Alias), same rules as the adapter's alias */
    static bool device_alias_request(std::string_view address, std::string_view alias, std::list<std::string> &request)
    {
        if (!valid_device_address(address) || !valid_alias(alias)) return false;
        request = {"BtDeviceAlias", std::string(address), std::string(alias)};
        return true;
    }

    static bool list_request(bool connected_only, int maximum_count,
                             std::list<std::string> &request)
    {
        if (maximum_count < 1 || maximum_count > CP0_BT_DEVICE_MAX) return false;
        request = {connected_only ? "BtConnectedList" : "BtList", std::to_string(maximum_count)};
        return true;
    }

    static bool scan_request(int maximum_count, std::list<std::string> &request)
    {
        if (maximum_count < 1 || maximum_count > CP0_BT_DEVICE_MAX) return false;
        request = {"BtScan", std::to_string(maximum_count)};
        return true;
    }

    static bool discovery_start_request(std::list<std::string> &request)
    {
        request = {"BtDiscoveryStart"};
        return true;
    }

    static std::list<std::string> discovery_start_request()
    {
        return {"BtDiscoveryStart"};
    }

    static bool discovery_stop_request(std::list<std::string> &request)
    {
        request = {"BtDiscoveryStop"};
        return true;
    }

    static std::list<std::string> discovery_stop_request()
    {
        return {"BtDiscoveryStop"};
    }

    static bool device_request(const char *command,
                               std::string_view address,
                               std::list<std::string> &request)
    {
        if (!command || !valid_device_address(address)) return false;
        const std::string command_text(command);
        if (command_text != "BtPair" && command_text != "BtCancelPairing" && command_text != "BtConnect" &&
            command_text != "BtDisconnect" && command_text != "BtRemove")
            return false;
        request = {command_text, std::string(address)};
        return true;
    }
};

namespace settings_bluetooth {

using DeviceRecord = settings_bluetooth_com::DeviceRecord;
using StatusRecord = settings_bluetooth_com::StatusRecord;

/* ------------------------------------------------------------------ helpers moved from the legacy page */

void copy_text(char *target, std::size_t capacity, std::string_view value);
void copy_device_record(const DeviceRecord &source, cp0_bt_device_t &target);
bool is_agent_authorization_method(std::string_view method);
std::string normalized_mac_text(std::string_view text);
/* Named Only: a device without a name, or whose name is only a MAC address, is hidden. */
bool should_hide_named_only_device(const DeviceRecord &device);

/* ------------------------------------------------------------------ list rules */

enum class ListMode { Paired, Scan };

/* What a list shows (the legacy page's rule): Named Only drops unnamed devices; Paired Devices keeps the paired and
 * the connected ones only. */
void filter_devices(std::vector<DeviceRecord> &devices, bool named_only, ListMode mode);

/* The row label: the name, or the address when the device has none. */
std::string device_label(const DeviceRecord &device);
/* The row value (the legacy page's state column): "Connected", "Paired", else the signal ("-60 dBm"). */
std::string device_state(const DeviceRecord &device);
/* "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF" (an agent request's device) -> "AA:BB:CC:DD:EE:FF"; "" when it is not one. */
std::string address_of_path(std::string_view object_path);

/* A failed action for the status line (the legacy page's text). */
std::string action_error_text(const std::string &data);
/* The adapter / device name check: empty when the name may be sent. */
std::string alias_error(std::string_view alias);

/* ------------------------------------------------------------------ agent prompts (BlueZ Agent1 requests) */

enum class PromptKind {
    Unsupported,
    PinCode,              /* RequestPinCode: type the PIN (1-16 characters) */
    Passkey,              /* RequestPasskey: type the 6-digit passkey the device shows */
    Confirmation,         /* RequestConfirmation: does the device show this 6-digit code? */
    Authorization,        /* RequestAuthorization: may the device connect? */
    ServiceAuthorization, /* AuthorizeService: may it use this service (UUID)? */
    DisplayPasskey,       /* DisplayPasskey: type this code on the device (a keyboard); no reply */
    DisplayPinCode,       /* DisplayPinCode: type this PIN on the device; no reply */
};
PromptKind prompt_kind(std::string_view method);
bool prompt_takes_input(PromptKind kind);
bool prompt_is_display(PromptKind kind);
std::size_t prompt_max_input(PromptKind kind); /* 16 (PIN), 6 (passkey), 0 */
/* The legacy dialog titles ("Bluetooth PIN Code", "Confirm Bluetooth pairing", ...). */
std::string prompt_title(PromptKind kind);
/* The input check before replying (the legacy page's texts): empty when the reply may be sent. */
std::string agent_input_error(std::string_view method, std::string_view text);

/* The session-scoped authorization of the legacy page: BlueZ may ask once per profile to authorize a connection the
 * user already allowed; the first decision is kept for that device in this pairing / connection session only.
 * request_device: the request's device, or the operation's address when it names none. */
class AgentSession {
public:
    /* A new explicit operation (or its failure, or a reset): no earlier decision carries over. */
    void reset();
    /* A request arrived. True: answer "yes" at once (a later authorization of the device already allowed). A numeric
     * comparison starts a fresh pairing session. */
    bool auto_accept(std::string_view method, const std::string &request_device);
    /* The user accepted the request. action_pending: an operation the user asked for is running. */
    void accepted(std::string_view method, const std::string &request_device, bool action_pending);

    const std::string &pairing_device() const { return pairing_device_; }
    const std::string &authorized_device() const { return authorized_device_; }

private:
    std::string pairing_device_;
    std::string authorized_device_;
};

/* ------------------------------------------------------------------ state machines of the native pages */

using Reply = std::function<void(int code, std::string data)>;
/* Send one Bluetooth API request; the reply comes later on the UI thread (or never, when the page went). */
using Transport = std::function<void(std::list<std::string> request, Reply reply)>;

/* Paired Devices / Scan: the legacy LvSettingBluetoothPage3 protocol without its drawing. Every request carries a
 * generation; a reply for an older one runs its stale handler (scan start / stop, actions) or is dropped. */
class DeviceListModel {
public:
    enum class Op { None, Pair, Connect, Disconnect, Forget, Rename };

    struct Hooks {
        Transport request;
        std::function<bool()> named_only;          /* the Named Only setting (read on every list) */
        std::function<void()> changed;             /* something shown changed */
        std::function<void(Op op, bool ok)> ended; /* an operation finished (after `changed`) */
        AgentSession *agent = nullptr;             /* reset when the user starts an operation or it fails */
    };

    DeviceListModel(ListMode mode, Hooks hooks);
    ~DeviceListModel();
    DeviceListModel(const DeviceListModel &) = delete;
    DeviceListModel &operator=(const DeviceListModel &) = delete;

    /* The page opened: the status, then the list (Paired Devices) or discovery (Scan). */
    void open();
    /* Leaving: discovery stops, a pairing in flight is cancelled. False while a failed pairing is being cleaned up
     * (the legacy page refuses Esc then), unless forced. */
    bool leave(bool force = false);
    /* Timers of the page: every second while the status is unresolved; every 1.5 s while discovering. */
    void retry_status();
    void scan_tick();
    void refresh();
    /* Scan: the toggle. On: a fresh discovery (status, stop, start: the legacy R); off: discovery stops, the list stays. */
    void set_scanning(bool on);

    /* Operations on one device (false: refused now). In Scan the discovery stops first; a pairing connects after. */
    bool pair(const std::string &address);
    bool connect(const std::string &address);
    bool disconnect(const std::string &address);
    bool forget(const std::string &address);
    bool rename(const std::string &address, const std::string &name);

    ListMode mode() const { return mode_; }
    bool status_known() const { return status_known_; }
    bool powered() const { return powered_; }
    /* the status says the adapter is off (the page shows the legacy "power is off" notice) */
    bool power_off() const { return status_known_ && !powered_; }
    const std::string &adapter_address() const { return adapter_address_; }
    const std::string &adapter_alias() const { return adapter_alias_; }
    const std::vector<DeviceRecord> &devices() const { return devices_; }
    const DeviceRecord *device(const std::string &address) const;
    bool listed() const { return listed_; }   /* a list arrived since the page opened */
    bool loading() const { return loading_; }
    bool scanning() const { return scan_wanted_; } /* Scan: the toggle's state */
    bool discovering() const { return discovery_active_; }
    Op op() const { return op_; }
    const std::string &op_address() const { return op_address_; }
    const std::string &op_message() const { return op_message_; }
    bool cleanup_pending() const { return pair_cleanup_pending_; }
    bool leaving() const { return leaving_; }
    /* The service's problem (status / list / discovery), red in the status line. */
    const std::string &error() const { return error_; }
    /* The last operation's result until the next one: "Paired with X", or the failure (result_failed()). */
    const std::string &result() const { return result_; }
    bool result_failed() const { return result_failed_; }
    /* The status line: the error, else the result, else "Checking Bluetooth...", "Bluetooth is off", or the
     * adapter's name and address. */
    std::string status_line(bool &error) const;

private:
    void call(std::list<std::string> request, Reply handler, Reply stale = {});
    void notify();
    void request_status();
    void start_scan();
    void stop_scan(std::function<void()> after_stop = {});
    void restart_scan();
    bool begin(Op op, const std::string &address, const char *message, bool reset_agent);
    void submit_after_scan(std::function<void()> submit);
    void submit_pair();
    void request_action(const char *command, const std::string &address);
    void cleanup_failed_pair(int code, std::string data);
    void cancel_action();
    void finish_action(int code, std::string data);

    ListMode mode_;
    Hooks hooks_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;

    bool status_known_ = false;
    bool powered_ = false;
    bool status_pending_ = false;
    bool retry_ = false;
    std::string adapter_address_;
    std::string adapter_alias_;
    std::vector<DeviceRecord> devices_;
    bool listed_ = false;
    bool list_pending_ = false;
    bool loading_ = false;
    bool leaving_ = false;
    std::string error_;

    bool scan_wanted_ = false;
    bool discovery_active_ = false;
    bool scan_start_pending_ = false;
    bool scan_stop_after_start_ = false;
    bool scan_stop_pending_ = false;
    bool scan_stop_required_ = false; /* a failed / unfinished start or stop may still own a BlueZ scan session */
    uint64_t scan_stop_request_id_ = 0;
    std::function<void()> scan_after_stop_;

    Op op_ = Op::None;
    std::string op_address_;
    std::string op_label_; /* the device's label when the operation started (for the result text) */
    std::string op_name_;  /* Rename: the new name */
    std::string op_message_;
    bool pair_in_flight_ = false;
    bool pair_cleanup_pending_ = false;
    bool action_waiting_for_scan_stop_ = false;
    std::string result_;
    bool result_failed_ = false;
};

/* The adapter alias page: the legacy LvSettingBluetoothAliasPage3 protocol without its drawing. */
class AliasModel {
public:
    struct Hooks {
        Transport request;
        std::function<void()> changed;
        std::function<void(const std::string &alias)> saved; /* the backend has this alias (read or set) */
        std::function<void()> done;                           /* saved: the page goes back */
    };

    /* initial: the alias the tree shows (an invalid one becomes "CyberdeckZero", as the legacy page does) */
    AliasModel(std::string initial, Hooks hooks);
    ~AliasModel();
    AliasModel(const AliasModel &) = delete;
    AliasModel &operator=(const AliasModel &) = delete;

    void open();
    void retry_status();
    /* The user changed the text: a late status read no longer replaces it. */
    void edited(const std::string &text);
    /* Enter: false when refused (the error says why, or the adapter is off: power_off()). */
    bool save(const std::string &text);

    const std::string &alias() const { return alias_; } /* the text the field should show */
    /* True once after alias() changed under the user (a status read, a failed save): the page puts it in the field. */
    bool take_text();
    bool status_known() const { return status_known_; }
    bool powered() const { return powered_; }
    bool power_off() const { return status_known_ && !powered_; }
    bool saving() const { return saving_; }
    const std::string &error() const { return error_; }

private:
    void call(std::list<std::string> request, Reply handler, Reply stale = {});
    void request_status();
    void notify();

    Hooks hooks_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    uint64_t generation_ = 1;
    std::string alias_;
    std::string backend_alias_;
    std::string alias_before_save_;
    bool alias_edited_ = false;
    bool text_changed_ = false;
    bool status_known_ = false;
    bool powered_ = false;
    bool status_pending_ = false;
    bool retry_ = false;
    bool saving_ = false;
    std::string error_;
};

} // namespace settings_bluetooth
