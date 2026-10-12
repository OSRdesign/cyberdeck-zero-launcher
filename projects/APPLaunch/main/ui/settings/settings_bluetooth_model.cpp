/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// Bluetooth model (see settings_bluetooth_model.hpp). The helpers at the top moved unchanged from
// settings_bluetooth_page.cpp; the state machines port the legacy pages' protocol (LvSettingBluetoothPage3 and
// LvSettingBluetoothAliasPage3: the same requests in the same order, the same texts) without their drawing.

#include "settings_bluetooth_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

namespace settings_bluetooth {

/* ------------------------------------------------------------------ helpers moved from the legacy page */

void copy_text(char *target, std::size_t capacity, std::string_view value)
{
    if (!target || capacity == 0) return;
    const std::size_t length = std::min(capacity - 1, value.size());
    if (length > 0) std::memcpy(target, value.data(), length);
    target[length] = '\0';
}

void copy_device_record(const DeviceRecord &source, cp0_bt_device_t &target)
{
    target = {};
    copy_text(target.address, sizeof(target.address), source.address);
    copy_text(target.name, sizeof(target.name), source.name);
    target.rssi = source.rssi;
    target.connected = source.connected ? 1 : 0;
    target.paired = source.paired ? 1 : 0;
    target.trusted = source.trusted ? 1 : 0;
}

bool is_agent_authorization_method(std::string_view method)
{
    return method == "RequestAuthorization" || method == "AuthorizeService";
}

std::string normalized_mac_text(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text) {
        if (std::isxdigit(character))
            result.push_back(static_cast<char>(std::tolower(character)));
        else if (character != ':' && character != '-' && character != '_' && character != ' ')
            return {};
    }
    return result;
}

bool should_hide_named_only_device(const DeviceRecord &device)
{
    if (device.name.empty()) return true;
    const std::string name_hex = normalized_mac_text(device.name);
    const std::string address_hex = normalized_mac_text(device.address);
    return !name_hex.empty() && (name_hex == address_hex || name_hex.size() == 12);
}

/* ------------------------------------------------------------------ list rules */

void filter_devices(std::vector<DeviceRecord> &devices, bool named_only, ListMode mode)
{
    if (named_only)
        devices.erase(std::remove_if(devices.begin(), devices.end(), should_hide_named_only_device), devices.end());
    if (mode == ListMode::Paired)
        devices.erase(std::remove_if(devices.begin(), devices.end(),
                                     [](const DeviceRecord &device) { return !device.connected && !device.paired; }),
                      devices.end());
}

std::string device_label(const DeviceRecord &device)
{
    return device.name.empty() ? device.address : device.name;
}

std::string device_state(const DeviceRecord &device)
{
    if (device.connected) return "Connected";
    if (device.paired) return "Paired";
    return std::to_string(device.rssi) + " dBm";
}

std::string address_of_path(std::string_view object_path)
{
    const std::size_t slash = object_path.rfind('/');
    std::string_view leaf = slash == std::string_view::npos ? object_path : object_path.substr(slash + 1);
    if (leaf.size() != 21 || leaf.substr(0, 4) != "dev_") return {};
    std::string address(leaf.substr(4));
    for (std::size_t i = 0; i < address.size(); ++i) {
        if ((i + 1) % 3 == 0) {
            if (address[i] != '_') return {};
            address[i] = ':';
        } else if (!std::isxdigit(static_cast<unsigned char>(address[i]))) {
            return {};
        }
    }
    return address;
}

std::string action_error_text(const std::string &data)
{
    return data.empty() ? "Bluetooth action failed." : "Bluetooth action failed: " + data;
}

std::string alias_error(std::string_view alias)
{
    return settings_bluetooth_com::valid_alias(alias) ? std::string() : std::string("Name must be 1-63 UTF-8 bytes.");
}

/* ------------------------------------------------------------------ agent prompts */

PromptKind prompt_kind(std::string_view method)
{
    if (method == "RequestPinCode") return PromptKind::PinCode;
    if (method == "RequestPasskey") return PromptKind::Passkey;
    if (method == "RequestConfirmation") return PromptKind::Confirmation;
    if (method == "RequestAuthorization") return PromptKind::Authorization;
    if (method == "AuthorizeService") return PromptKind::ServiceAuthorization;
    if (method == "DisplayPasskey") return PromptKind::DisplayPasskey;
    if (method == "DisplayPinCode") return PromptKind::DisplayPinCode;
    return PromptKind::Unsupported;
}

bool prompt_takes_input(PromptKind kind)
{
    return kind == PromptKind::PinCode || kind == PromptKind::Passkey;
}

bool prompt_is_display(PromptKind kind)
{
    return kind == PromptKind::DisplayPasskey || kind == PromptKind::DisplayPinCode;
}

std::size_t prompt_max_input(PromptKind kind)
{
    return kind == PromptKind::PinCode ? 16 : kind == PromptKind::Passkey ? 6 : 0;
}

std::string prompt_title(PromptKind kind)
{
    switch (kind) {
    case PromptKind::Confirmation: return "Confirm Bluetooth pairing";
    case PromptKind::Authorization: return "Allow device connection?";
    case PromptKind::ServiceAuthorization: return "Allow Bluetooth service?";
    case PromptKind::Passkey:
    case PromptKind::DisplayPasskey: return "Bluetooth Passkey";
    case PromptKind::PinCode:
    case PromptKind::DisplayPinCode: return "Bluetooth PIN Code";
    case PromptKind::Unsupported: break;
    }
    return {};
}

std::string agent_input_error(std::string_view method, std::string_view text)
{
    const bool passkey = method == "RequestPasskey";
    const bool confirmation = method == "RequestConfirmation" || method == "RequestAuthorization" ||
                              method == "AuthorizeService";
    if (confirmation) return {};
    if ((passkey && text.size() != 6) || (!passkey && (text.empty() || text.size() > 16)))
        return passkey ? "Passkey must be 6 digits." : "PIN must be 1-16 characters.";
    if (passkey && !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return "Passkey must be 6 digits.";
    return {};
}

void AgentSession::reset()
{
    pairing_device_.clear();
    authorized_device_.clear();
}

bool AgentSession::auto_accept(std::string_view method, const std::string &request_device)
{
    // A new numeric-comparison request starts a fresh pairing session; never carry an earlier connection approval
    // into that session.
    if (method == "RequestConfirmation") reset();
    // BlueZ may ask for authorization once per profile after the user has already approved the connection: keep the
    // first decision for that same device in this session.
    return is_agent_authorization_method(method) && !authorized_device_.empty() &&
           request_device == authorized_device_;
}

void AgentSession::accepted(std::string_view method, const std::string &request_device, bool action_pending)
{
    if (method == "RequestConfirmation") pairing_device_ = request_device;
    if (is_agent_authorization_method(method) && (action_pending || request_device == pairing_device_))
        authorized_device_ = request_device;
}

/* ------------------------------------------------------------------ DeviceListModel */

namespace {

const char *message_of(DeviceListModel::Op op)
{
    switch (op) {
    case DeviceListModel::Op::Pair: return "Pairing...";
    case DeviceListModel::Op::Connect: return "Connecting...";
    case DeviceListModel::Op::Disconnect: return "Disconnecting...";
    case DeviceListModel::Op::Forget: return "Removing...";
    case DeviceListModel::Op::Rename: return "Renaming...";
    case DeviceListModel::Op::None: break;
    }
    return "";
}

std::string success_text(DeviceListModel::Op op, const std::string &label, const std::string &name)
{
    switch (op) {
    case DeviceListModel::Op::Pair: return "Paired with " + label;
    case DeviceListModel::Op::Connect: return "Connected to " + label;
    case DeviceListModel::Op::Disconnect: return "Disconnected from " + label;
    case DeviceListModel::Op::Forget: return "Forgot " + label;
    case DeviceListModel::Op::Rename: return "Renamed to " + name;
    case DeviceListModel::Op::None: break;
    }
    return {};
}

} // namespace

DeviceListModel::DeviceListModel(ListMode mode, Hooks hooks) : mode_(mode), hooks_(std::move(hooks)) {}

DeviceListModel::~DeviceListModel()
{
    alive_.reset(); /* replies still on their way are dropped */
}

void DeviceListModel::notify()
{
    if (hooks_.changed) hooks_.changed();
}

void DeviceListModel::call(std::list<std::string> request, Reply handler, Reply stale)
{
    if (!hooks_.request) {
        if (handler) handler(-1, "Bluetooth service unavailable");
        return;
    }
    const std::weak_ptr<bool> alive = alive_;
    const uint64_t generation = generation_;
    hooks_.request(std::move(request), [this, alive, generation, handler = std::move(handler),
                                        stale = std::move(stale)](int code, std::string data) {
        if (alive.expired()) return;
        if (generation != generation_) {
            if (stale) stale(code, std::move(data));
            return;
        }
        if (handler) handler(code, std::move(data));
    });
}

const DeviceRecord *DeviceListModel::device(const std::string &address) const
{
    for (const DeviceRecord &record : devices_)
        if (record.address == address) return &record;
    return nullptr;
}

void DeviceListModel::open()
{
    if (mode_ == ListMode::Scan) scan_wanted_ = true;
    request_status();
}

void DeviceListModel::request_status()
{
    if (status_pending_ || leaving_) return;
    status_pending_ = true;
    call(settings_bluetooth_com::status_request(), [this](int code, std::string data) {
        status_pending_ = false;
        StatusRecord status;
        const bool decoded = code == 0 && settings_bluetooth_com::decode_status(data, status);
        if (!decoded) {
            ++generation_;
            status_known_ = false;
            powered_ = false;
            adapter_address_.clear();
            devices_.clear();
            error_ = "Bluetooth service unavailable.";
            stop_scan();
            retry_ = true;
            notify();
            return;
        }
        powered_ = status.powered;
        adapter_address_ = status.address;
        adapter_alias_ = status.alias;
        // A powered-off adapter still has a stable address. An empty address means the adapter / object manager is
        // not ready yet: keep retrying instead of a false "power is off".
        if (!powered_ && adapter_address_.empty()) {
            ++generation_;
            status_known_ = false;
            devices_.clear();
            error_ = "Bluetooth adapter unavailable. Retrying...";
            stop_scan();
            retry_ = true;
            notify();
            return;
        }
        status_known_ = true;
        error_.clear();
        retry_ = false;
        if (!powered_) {
            ++generation_;
            devices_.clear();
            scan_wanted_ = false;
            stop_scan();
            notify();
            return;
        }
        notify();
        if (mode_ == ListMode::Scan) {
            if (scan_wanted_) stop_scan([this] { start_scan(); });
        } else {
            refresh();
        }
    }, [this](int, std::string) { status_pending_ = false; });
}

void DeviceListModel::retry_status()
{
    if (leaving_ || !retry_ || status_known_ || status_pending_) return;
    request_status();
}

void DeviceListModel::refresh()
{
    if (!status_known_ || !powered_ || op_ != Op::None || list_pending_ || leaving_) return;
    std::list<std::string> request;
    if (!settings_bluetooth_com::list_request(false, CP0_BT_DEVICE_MAX, request)) return;
    list_pending_ = true;
    loading_ = true;
    notify();
    call(std::move(request), [this](int code, std::string data) {
        list_pending_ = false;
        loading_ = false;
        if (!status_known_ || !powered_) {
            devices_.clear();
            error_ = "Bluetooth is off. Enable Power first.";
        } else {
            std::vector<DeviceRecord> decoded;
            if (!settings_bluetooth_com::decode_device_list_reply(code, data, decoded, CP0_BT_DEVICE_MAX)) {
                devices_.clear();
                error_ = "Bluetooth device list unavailable.";
            } else {
                filter_devices(decoded, hooks_.named_only ? hooks_.named_only() : true, mode_);
                devices_ = std::move(decoded);
                error_.clear();
            }
        }
        listed_ = true;
        notify();
    });
}

void DeviceListModel::scan_tick()
{
    if (!discovery_active_ || op_ != Op::None) return;
    refresh();
}

void DeviceListModel::start_scan()
{
    if (mode_ != ListMode::Scan || !powered_ || leaving_ || scan_start_pending_ || scan_stop_pending_ ||
        discovery_active_)
        return;
    scan_start_pending_ = true;
    scan_stop_required_ = true;
    loading_ = true;
    error_.clear();
    notify();
    const auto complete_start = [this](int code, std::string data) {
        scan_start_pending_ = false;
        if (leaving_) {
            discovery_active_ = false;
            loading_ = false;
            return;
        }
        if (scan_stop_after_start_) {
            scan_stop_after_start_ = false;
            const auto continuation = std::move(scan_after_stop_);
            scan_after_stop_ = nullptr;
            if (!settings_bluetooth_com::success_without_payload(code, data)) {
                discovery_active_ = false;
                loading_ = false;
                if (continuation) continuation();
                return;
            }
            discovery_active_ = true;
            stop_scan(continuation);
            return;
        }
        if (!settings_bluetooth_com::success_without_payload(code, data) || !powered_) {
            discovery_active_ = false;
            scan_wanted_ = false;
            error_ = powered_ ? "Bluetooth scan unavailable." : "Bluetooth is off. Enable Power first.";
            loading_ = false;
            notify();
            return;
        }
        discovery_active_ = true;
        refresh();
        notify();
    };
    call(settings_bluetooth_com::discovery_start_request(), complete_start, complete_start);
}

void DeviceListModel::stop_scan(std::function<void()> after_stop)
{
    const bool needs_stop = discovery_active_ || scan_start_pending_ || scan_stop_required_;
    if (scan_start_pending_) {
        scan_stop_after_start_ = true;
        if (after_stop) {
            auto previous = std::move(scan_after_stop_);
            scan_after_stop_ = [previous = std::move(previous), after_stop = std::move(after_stop)]() mutable {
                if (previous) previous();
                if (after_stop) after_stop();
            };
        }
        discovery_active_ = false;
        loading_ = false;
        return;
    }
    discovery_active_ = false;
    scan_start_pending_ = false;
    loading_ = false;

    if (scan_stop_pending_) {
        if (after_stop) {
            auto previous = std::move(scan_after_stop_);
            scan_after_stop_ = [previous = std::move(previous), after_stop = std::move(after_stop)]() mutable {
                if (previous) previous();
                if (after_stop) after_stop();
            };
        }
        return;
    }
    if (!needs_stop) {
        if (after_stop) after_stop();
        return;
    }
    scan_stop_pending_ = true;
    scan_stop_required_ = true;
    scan_after_stop_ = std::move(after_stop);
    const uint64_t stop_request_id = ++scan_stop_request_id_;
    const auto complete_stop = [this, stop_request_id](int code, std::string data) {
        if (!scan_stop_pending_ || stop_request_id != scan_stop_request_id_) return;
        scan_stop_pending_ = false;
        const auto continuation = std::move(scan_after_stop_);
        scan_after_stop_ = nullptr;
        if (leaving_) return;
        if (!settings_bluetooth_com::success_without_payload(code, data)) {
            error_ = "Bluetooth scan stop failed.";
            if (action_waiting_for_scan_stop_) {
                action_waiting_for_scan_stop_ = false;
                const Op op = op_;
                cancel_action();
                if (hooks_.ended) hooks_.ended(op, false);
            }
            notify();
            return;
        }
        scan_stop_required_ = false;
        if (continuation) continuation();
        notify();
    };
    call(settings_bluetooth_com::discovery_stop_request(), complete_stop, complete_stop);
}

/* The legacy R: the status again, then a fresh discovery (stop, start). */
void DeviceListModel::restart_scan()
{
    if (mode_ != ListMode::Scan || op_ != Op::None || leaving_ || scan_start_pending_ || scan_stop_pending_ ||
        status_pending_)
        return;
    ++generation_;
    list_pending_ = false;
    status_known_ = false;
    error_.clear();
    devices_.clear();
    scan_wanted_ = true;
    notify();
    request_status();
}

void DeviceListModel::set_scanning(bool on)
{
    if (mode_ != ListMode::Scan || leaving_ || op_ != Op::None) return;
    result_.clear();
    if (on) {
        restart_scan();
        return;
    }
    scan_wanted_ = false;
    stop_scan();
    notify();
}

bool DeviceListModel::begin(Op op, const std::string &address, const char *message, bool reset_agent)
{
    if (op_ != Op::None || leaving_ || status_pending_ || scan_start_pending_ || scan_stop_pending_ || !powered_)
        return false;
    if (!settings_bluetooth_com::valid_device_address(address)) {
        error_ = "Bluetooth device address is invalid.";
        notify();
        return false;
    }
    // A new explicit operation requires a fresh authorization decision.
    if (reset_agent && hooks_.agent) hooks_.agent->reset();
    const DeviceRecord *record = device(address);
    op_label_ = record ? device_label(*record) : address;
    op_ = op;
    op_address_ = address;
    ++generation_;
    list_pending_ = false;
    loading_ = false;
    op_message_ = message;
    result_.clear();
    result_failed_ = false;
    notify();
    return true;
}

/* In Scan the discovery stops before the operation is sent (BlueZ pairs badly while discovering). */
void DeviceListModel::submit_after_scan(std::function<void()> submit)
{
    if (mode_ == ListMode::Scan) {
        action_waiting_for_scan_stop_ = discovery_active_ || scan_start_pending_ || scan_stop_pending_ ||
                                        scan_stop_required_;
        if (action_waiting_for_scan_stop_) {
            stop_scan([this, submit = std::move(submit)] {
                action_waiting_for_scan_stop_ = false;
                submit();
            });
        } else {
            stop_scan();
            submit();
        }
        return;
    }
    submit();
}

bool DeviceListModel::pair(const std::string &address)
{
    if (!begin(Op::Pair, address, message_of(Op::Pair), true)) return false;
    submit_after_scan([this] { submit_pair(); });
    return true;
}

bool DeviceListModel::connect(const std::string &address)
{
    if (!begin(Op::Connect, address, message_of(Op::Connect), true)) return false;
    submit_after_scan([this] { request_action("BtConnect", op_address_); });
    return true;
}

bool DeviceListModel::disconnect(const std::string &address)
{
    if (!begin(Op::Disconnect, address, message_of(Op::Disconnect), true)) return false;
    submit_after_scan([this] { request_action("BtDisconnect", op_address_); });
    return true;
}

bool DeviceListModel::forget(const std::string &address)
{
    if (!begin(Op::Forget, address, message_of(Op::Forget), false)) return false;
    submit_after_scan([this] { request_action("BtRemove", op_address_); });
    return true;
}

bool DeviceListModel::rename(const std::string &address, const std::string &name)
{
    std::list<std::string> request;
    if (!settings_bluetooth_com::device_alias_request(address, name, request)) {
        result_ = alias_error(name);
        if (result_.empty()) result_ = "Bluetooth device address is invalid.";
        result_failed_ = true;
        notify();
        return false;
    }
    if (!begin(Op::Rename, address, message_of(Op::Rename), false)) return false;
    op_name_ = name;
    submit_after_scan([this, request] {
        call(request, [this](int code, std::string data) { finish_action(code, std::move(data)); },
             [this](int, std::string) {
                 if (op_ == Op::None) return;
                 cancel_action();
                 notify();
             });
    });
    return true;
}

void DeviceListModel::submit_pair()
{
    if (op_ != Op::Pair) return;
    std::list<std::string> request;
    if (!settings_bluetooth_com::device_request("BtPair", op_address_, request)) {
        finish_action(-1, "");
        return;
    }
    pair_in_flight_ = true;
    call(std::move(request), [this](int code, std::string data) {
        pair_in_flight_ = false;
        if (!settings_bluetooth_com::success_without_payload(code, data)) {
            cleanup_failed_pair(code, std::move(data));
            return;
        }
        op_message_ = "Connecting...";
        notify();
        request_action("BtConnect", op_address_);
    }, [this](int, std::string) {
        pair_in_flight_ = false;
        if (op_ == Op::None) return;
        cancel_action();
        notify();
    });
}

void DeviceListModel::request_action(const char *command, const std::string &address)
{
    std::list<std::string> request;
    if (!settings_bluetooth_com::device_request(command, address, request)) {
        finish_action(-1, "");
        return;
    }
    call(std::move(request), [this](int code, std::string data) { finish_action(code, std::move(data)); },
         [this](int, std::string) {
             if (op_ == Op::None) return;
             cancel_action();
             notify();
         });
}

/* CancelPairing releases BlueZ's bonding / agent state before the temporary device object is removed; both are best
 * effort, then the pairing's own failure is reported. */
void DeviceListModel::cleanup_failed_pair(int code, std::string data)
{
    if (op_ == Op::None || pair_cleanup_pending_ || op_address_.empty()) {
        finish_action(code, std::move(data));
        return;
    }
    pair_cleanup_pending_ = true;
    op_message_ = "Cleaning up pairing...";
    notify();
    std::list<std::string> cancel;
    if (!settings_bluetooth_com::device_request("BtCancelPairing", op_address_, cancel)) {
        pair_cleanup_pending_ = false;
        finish_action(code, std::move(data));
        return;
    }
    const auto remove = [this, code, data](int, std::string) {
        std::list<std::string> request;
        if (!settings_bluetooth_com::device_request("BtRemove", op_address_, request)) {
            pair_cleanup_pending_ = false;
            finish_action(code, data);
            return;
        }
        const auto done = [this, code, data](int, std::string) {
            pair_cleanup_pending_ = false;
            finish_action(code, data);
        };
        call(std::move(request), done, done);
    };
    call(std::move(cancel), remove, remove);
}

void DeviceListModel::cancel_action()
{
    action_waiting_for_scan_stop_ = false;
    pair_cleanup_pending_ = false;
    pair_in_flight_ = false;
    op_ = Op::None;
    op_message_.clear();
    op_address_.clear();
}

void DeviceListModel::finish_action(int code, std::string data)
{
    const bool succeeded = settings_bluetooth_com::success_without_payload(code, data);
    const Op op = op_;
    const std::string label = op_label_;
    const std::string name = op_name_;
    cancel_action();
    if (!succeeded) {
        if (hooks_.agent) hooks_.agent->reset();
        result_ = action_error_text(data);
        result_failed_ = true;
    } else {
        result_ = success_text(op, label, name);
        result_failed_ = false;
    }
    notify();
    if (hooks_.ended) hooks_.ended(op, succeeded);
    if (mode_ == ListMode::Scan) restart_scan();
    else refresh();
}

bool DeviceListModel::leave(bool force)
{
    if (leaving_) return true;
    // Do not tear the page down while the Pair-failure cleanup chain still has to remove the temporary BlueZ device.
    if (pair_cleanup_pending_ && !force) return false;
    const bool cancel_pair = op_ != Op::None && pair_in_flight_;
    const std::string pairing_address = cancel_pair ? op_address_ : std::string();
    leaving_ = true;
    ++generation_;
    cancel_action();
    stop_scan();
    // An in-flight discovery stop may own a continuation that would otherwise submit Pair after the user left.
    scan_after_stop_ = nullptr;
    scan_stop_after_start_ = false;
    if (cancel_pair) {
        std::list<std::string> request;
        if (settings_bluetooth_com::device_request("BtCancelPairing", pairing_address, request))
            call(std::move(request), {}, {});
    }
    return true;
}

std::string DeviceListModel::status_line(bool &error) const
{
    error = false;
    if (!error_.empty()) {
        error = true;
        return error_;
    }
    if (!result_.empty()) {
        error = result_failed_;
        return result_;
    }
    if (!status_known_) return "Checking Bluetooth...";
    if (!powered_) return "Bluetooth is off";
    if (adapter_alias_.empty()) return adapter_address_;
    return adapter_alias_ + "  " + adapter_address_;
}

/* ------------------------------------------------------------------ AliasModel */

AliasModel::AliasModel(std::string initial, Hooks hooks) : hooks_(std::move(hooks)), alias_(std::move(initial))
{
    if (!settings_bluetooth_com::valid_alias(alias_)) alias_ = "CyberdeckZero";
    backend_alias_ = alias_;
}

AliasModel::~AliasModel()
{
    alive_.reset();
}

void AliasModel::notify()
{
    if (hooks_.changed) hooks_.changed();
}

void AliasModel::call(std::list<std::string> request, Reply handler, Reply stale)
{
    if (!hooks_.request) {
        if (handler) handler(-1, "Bluetooth service unavailable");
        return;
    }
    const std::weak_ptr<bool> alive = alive_;
    const uint64_t generation = generation_;
    hooks_.request(std::move(request), [this, alive, generation, handler = std::move(handler),
                                        stale = std::move(stale)](int code, std::string data) {
        if (alive.expired()) return;
        if (generation != generation_) {
            if (stale) stale(code, std::move(data));
        } else if (handler) {
            handler(code, std::move(data));
        }
    });
}

void AliasModel::open()
{
    request_status();
}

void AliasModel::request_status()
{
    if (status_pending_) return;
    status_pending_ = true;
    call(settings_bluetooth_com::status_request(), [this](int code, std::string data) {
        status_pending_ = false;
        StatusRecord status;
        status_known_ = code == 0 && settings_bluetooth_com::decode_status(data, status);
        if (!status_known_) {
            powered_ = false;
            error_ = "Bluetooth service unavailable.";
            retry_ = true;
            notify();
            return;
        }
        // A powered-off adapter still reports its stable address; an empty one means BlueZ is not ready yet.
        if (!status.powered && status.address.empty()) {
            status_known_ = false;
            powered_ = false;
            error_ = "Bluetooth adapter unavailable. Retrying...";
            retry_ = true;
            notify();
            return;
        }
        powered_ = status.powered;
        retry_ = false;
        if (settings_bluetooth_com::valid_alias(status.alias)) {
            backend_alias_ = status.alias;
            if (!alias_edited_) {
                alias_ = status.alias;
                text_changed_ = true;
                if (hooks_.saved) hooks_.saved(status.alias);
            }
        }
        if (powered_) error_.clear();
        notify();
    }, [this](int, std::string) {
        status_pending_ = false;
        retry_ = true;
    });
}

void AliasModel::retry_status()
{
    if (!retry_ || status_known_ || status_pending_) return;
    request_status();
}

void AliasModel::edited(const std::string &text)
{
    if (saving_) return;
    alias_ = text;
    alias_edited_ = true;
    if (!error_.empty() && status_known_) {
        error_.clear();
        notify();
    }
}

bool AliasModel::save(const std::string &text)
{
    if (saving_) return false;
    alias_ = text;
    const std::string invalid = alias_error(alias_);
    if (!invalid.empty()) {
        error_ = invalid;
        notify();
        return false;
    }
    if (!status_known_) {
        if (error_.empty()) error_ = "Bluetooth service unavailable.";
        notify();
        return false;
    }
    if (!powered_) {
        notify(); /* power_off(): the page shows the legacy notice */
        return false;
    }
    std::list<std::string> request;
    if (!settings_bluetooth_com::alias_request(alias_, request)) {
        error_ = "Name contains unsupported characters.";
        notify();
        return false;
    }
    alias_before_save_ = backend_alias_;
    saving_ = true;
    error_.clear();
    notify();
    call(std::move(request), [this](int code, std::string data) {
        saving_ = false;
        if (!settings_bluetooth_com::success_without_payload(code, data)) {
            alias_ = alias_before_save_;
            alias_edited_ = false;
            text_changed_ = true;
            error_ = "Set alias failed.";
            notify();
            return;
        }
        backend_alias_ = alias_;
        alias_edited_ = false;
        if (hooks_.saved) hooks_.saved(alias_);
        notify();
        if (hooks_.done) hooks_.done();
    }, [this](int, std::string) {
        saving_ = false;
        alias_ = alias_before_save_;
        alias_edited_ = false;
        text_changed_ = true;
        error_ = "Set alias cancelled.";
        notify();
    });
    return true;
}

bool AliasModel::take_text()
{
    const bool changed = text_changed_;
    text_changed_ = false;
    return changed;
}

} // namespace settings_bluetooth
