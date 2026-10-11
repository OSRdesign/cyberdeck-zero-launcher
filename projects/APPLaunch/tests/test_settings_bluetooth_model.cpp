/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

// The Bluetooth model of the native Settings pages (task 014 P3c, settings_bluetooth_model.hpp): the Named Only filter,
// the agent prompt rules and texts, the session-scoped authorization, and the state machines (Paired Devices, Scan,
// the adapter alias) driven through a fake transport: the requests they send, in order, and what they show.

#include "../main/ui/settings/settings_bluetooth_model.hpp"

#include <cassert>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

namespace {

using settings_bluetooth::AgentSession;
using settings_bluetooth::AliasModel;
using settings_bluetooth::DeviceListModel;
using settings_bluetooth::DeviceRecord;
using settings_bluetooth::ListMode;
using settings_bluetooth::PromptKind;

DeviceRecord record(const char *address, const char *name, bool paired, bool connected, int rssi = -60)
{
    DeviceRecord device;
    device.address = address;
    device.name = name;
    device.paired = paired;
    device.connected = connected;
    device.rssi = rssi;
    return device;
}

std::vector<std::string> addresses(const std::vector<DeviceRecord> &devices)
{
    std::vector<std::string> result;
    for (const auto &device : devices) result.push_back(device.address);
    return result;
}

/* The service as the page's worker would call it: requests are kept until the test answers them. */
struct FakeService {
    struct Pending {
        std::list<std::string> request;
        settings_bluetooth::Reply reply;
    };
    std::deque<Pending> pending;
    std::vector<std::string> log; /* every request, "Command arg..." */

    settings_bluetooth::Transport transport()
    {
        return [this](std::list<std::string> request, settings_bluetooth::Reply reply) {
            std::string line;
            for (const auto &part : request) line += (line.empty() ? "" : " ") + part;
            log.push_back(line);
            pending.push_back({std::move(request), std::move(reply)});
        };
    }

    std::string next() const { return pending.empty() ? std::string() : pending.front().request.front(); }

    /* answer the oldest request, which must be `command` */
    void answer(const char *command, int code, const std::string &data = {})
    {
        assert(!pending.empty());
        if (pending.front().request.front() != command) {
            std::fprintf(stderr, "expected %s, next is %s\n", command, pending.front().request.front().c_str());
            assert(false);
        }
        Pending item = std::move(pending.front());
        pending.pop_front();
        if (item.reply) item.reply(code, data);
    }
};

const char *kStatusOn = "1\t00:11:22:33:44:55\t0\tdeck";
const char *kStatusOff = "0\t00:11:22:33:44:55\t0\tdeck";

std::string device_lines(const std::vector<DeviceRecord> &devices)
{
    std::string text;
    for (const auto &device : devices)
        text += device.address + "\t" + std::to_string(device.rssi) + "\t" + (device.connected ? "1" : "0") + "\t" +
                (device.paired ? "1" : "0") + "\t0\t" + device.name + "\n";
    return text;
}

const std::vector<DeviceRecord> kDevices = {
    record("02:00:00:00:00:01", "Travel Keyboard", true, true),
    record("02:00:00:00:00:02", "Studio Headphones", true, false),
    record("02:00:00:00:00:03", "Field Phone", false, false, -48),
    record("02:00:00:00:00:04", "", false, false, -80),                  /* no name */
    record("02:00:00:00:00:05", "02-00-00-00-00-05", false, false, -70), /* its address as the name */
};

void test_filter_and_texts()
{
    std::vector<DeviceRecord> devices = kDevices;
    settings_bluetooth::filter_devices(devices, true, ListMode::Scan);
    assert((addresses(devices) ==
            std::vector<std::string>{"02:00:00:00:00:01", "02:00:00:00:00:02", "02:00:00:00:00:03"}));
    devices = kDevices;
    settings_bluetooth::filter_devices(devices, false, ListMode::Scan);
    assert(devices.size() == kDevices.size());
    devices = kDevices;
    settings_bluetooth::filter_devices(devices, false, ListMode::Paired);
    assert((addresses(devices) == std::vector<std::string>{"02:00:00:00:00:01", "02:00:00:00:00:02"}));
    // any 12-hex-digit name is a MAC address, even another device's
    assert(settings_bluetooth::should_hide_named_only_device(record("02:00:00:00:00:09", "AABBCCDDEEFF", false, false)));
    assert(!settings_bluetooth::should_hide_named_only_device(record("02:00:00:00:00:09", "Cafe", false, false)));

    assert(settings_bluetooth::device_label(kDevices[0]) == "Travel Keyboard");
    assert(settings_bluetooth::device_label(kDevices[3]) == "02:00:00:00:00:04");
    assert(settings_bluetooth::device_state(kDevices[0]) == "Connected");
    assert(settings_bluetooth::device_state(kDevices[1]) == "Paired");
    assert(settings_bluetooth::device_state(kDevices[2]) == "-48 dBm");

    assert(settings_bluetooth::address_of_path("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_0F") == "AA:BB:CC:DD:EE:0F");
    assert(settings_bluetooth::address_of_path("/org/bluez/hci0").empty());
    assert(settings_bluetooth::address_of_path("/org/bluez/hci0/dev_AA_BB_CC_DD_EE").empty());
    assert(settings_bluetooth::address_of_path("/org/bluez/hci0/dev_AA:BB_CC_DD_EE_0F").empty());
    assert(settings_bluetooth::address_of_path("").empty());

    assert(settings_bluetooth::action_error_text("") == "Bluetooth action failed.");
    assert(settings_bluetooth::action_error_text("Page Timeout") == "Bluetooth action failed: Page Timeout");
    assert(settings_bluetooth::alias_error("deck").empty());
    assert(settings_bluetooth::alias_error("") == "Name must be 1-63 UTF-8 bytes.");
    assert(settings_bluetooth::alias_error(std::string(64, 'a')) == "Name must be 1-63 UTF-8 bytes.");
    assert(settings_bluetooth::alias_error("bad\tname") == "Name must be 1-63 UTF-8 bytes.");
    assert(settings_bluetooth::alias_error("\xc3\x28") == "Name must be 1-63 UTF-8 bytes."); /* broken UTF-8 */

    std::list<std::string> request;
    assert(settings_bluetooth_com::device_alias_request("02:00:00:00:00:01", "Desk", request));
    assert((request == std::list<std::string>{"BtDeviceAlias", "02:00:00:00:00:01", "Desk"}));
    assert(!settings_bluetooth_com::device_alias_request("02:00:00:00:00", "Desk", request));
    assert(!settings_bluetooth_com::device_alias_request("02:00:00:00:00:01", "", request));
}

void test_prompt_rules()
{
    assert(settings_bluetooth::prompt_kind("RequestPinCode") == PromptKind::PinCode);
    assert(settings_bluetooth::prompt_kind("RequestPasskey") == PromptKind::Passkey);
    assert(settings_bluetooth::prompt_kind("RequestConfirmation") == PromptKind::Confirmation);
    assert(settings_bluetooth::prompt_kind("RequestAuthorization") == PromptKind::Authorization);
    assert(settings_bluetooth::prompt_kind("AuthorizeService") == PromptKind::ServiceAuthorization);
    assert(settings_bluetooth::prompt_kind("DisplayPasskey") == PromptKind::DisplayPasskey);
    assert(settings_bluetooth::prompt_kind("DisplayPinCode") == PromptKind::DisplayPinCode);
    assert(settings_bluetooth::prompt_kind("Cancel") == PromptKind::Unsupported);
    assert(settings_bluetooth::prompt_takes_input(PromptKind::PinCode));
    assert(settings_bluetooth::prompt_takes_input(PromptKind::Passkey));
    assert(!settings_bluetooth::prompt_takes_input(PromptKind::Confirmation));
    assert(settings_bluetooth::prompt_is_display(PromptKind::DisplayPasskey));
    assert(!settings_bluetooth::prompt_is_display(PromptKind::Passkey));
    assert(settings_bluetooth::prompt_max_input(PromptKind::PinCode) == 16);
    assert(settings_bluetooth::prompt_max_input(PromptKind::Passkey) == 6);
    assert(settings_bluetooth::prompt_title(PromptKind::Confirmation) == "Confirm Bluetooth pairing");
    assert(settings_bluetooth::prompt_title(PromptKind::PinCode) == "Bluetooth PIN Code");
    assert(settings_bluetooth::prompt_title(PromptKind::Passkey) == "Bluetooth Passkey");

    // the legacy page's input checks and texts
    assert(settings_bluetooth::agent_input_error("RequestPinCode", "0000").empty());
    assert(settings_bluetooth::agent_input_error("RequestPinCode", "") == "PIN must be 1-16 characters.");
    assert(settings_bluetooth::agent_input_error("RequestPinCode", std::string(17, '1')) ==
           "PIN must be 1-16 characters.");
    assert(settings_bluetooth::agent_input_error("RequestPasskey", "123456").empty());
    assert(settings_bluetooth::agent_input_error("RequestPasskey", "12345") == "Passkey must be 6 digits.");
    assert(settings_bluetooth::agent_input_error("RequestPasskey", "12345a") == "Passkey must be 6 digits.");
    assert(settings_bluetooth::agent_input_error("RequestConfirmation", "").empty());
    assert(settings_bluetooth::agent_input_error("AuthorizeService", "").empty());
}

void test_agent_session()
{
    const std::string phone = "/org/bluez/hci0/dev_02_00_00_00_00_03";
    const std::string other = "/org/bluez/hci0/dev_02_00_00_00_00_02";
    AgentSession session;
    // nothing allowed yet: an authorization is asked
    assert(!session.auto_accept("AuthorizeService", phone));
    // the user confirms the pairing code, then allows the connection: later profiles of that device pass at once
    assert(!session.auto_accept("RequestConfirmation", phone));
    session.accepted("RequestConfirmation", phone, false);
    assert(session.pairing_device() == phone);
    assert(!session.auto_accept("RequestAuthorization", phone));
    session.accepted("RequestAuthorization", phone, false);
    assert(session.authorized_device() == phone);
    assert(session.auto_accept("AuthorizeService", phone));
    assert(!session.auto_accept("AuthorizeService", other)); /* only that device */
    assert(!session.auto_accept("RequestPinCode", phone));   /* never a credential */
    // a new numeric comparison starts a fresh session
    assert(!session.auto_accept("RequestConfirmation", other));
    assert(session.authorized_device().empty() && session.pairing_device().empty());
    assert(!session.auto_accept("AuthorizeService", phone));
    // an authorization during an operation the user asked for is kept for its device
    session.accepted("AuthorizeService", other, true);
    assert(session.auto_accept("AuthorizeService", other));
    // another device's authorization outside an operation is not remembered
    session.reset();
    session.accepted("AuthorizeService", other, false);
    assert(session.authorized_device().empty());
}

struct ListHarness {
    FakeService service;
    AgentSession agent;
    bool named_only = true;
    int changes = 0;
    std::vector<std::pair<DeviceListModel::Op, bool>> ended;
    std::unique_ptr<DeviceListModel> model;

    explicit ListHarness(ListMode mode)
    {
        DeviceListModel::Hooks hooks;
        hooks.request = service.transport();
        hooks.named_only = [this] { return named_only; };
        hooks.changed = [this] { ++changes; };
        hooks.ended = [this](DeviceListModel::Op op, bool ok) { ended.emplace_back(op, ok); };
        hooks.agent = &agent;
        model = std::make_unique<DeviceListModel>(mode, std::move(hooks));
    }
};

void test_paired_list()
{
    ListHarness h(ListMode::Paired);
    bool error = false;
    assert(h.model->status_line(error) == "Checking Bluetooth...");
    h.model->open();
    assert(h.service.log == std::vector<std::string>{"BtStatus"});
    h.service.answer("BtStatus", 0, kStatusOn);
    assert(h.model->status_known() && h.model->powered() && !h.model->power_off());
    assert(h.service.next() == "BtList");
    assert(h.model->loading() && !h.model->listed());
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));
    assert(h.model->listed() && !h.model->loading());
    assert((addresses(h.model->devices()) == std::vector<std::string>{"02:00:00:00:00:01", "02:00:00:00:00:02"}));
    assert(h.model->status_line(error) == "deck  00:11:22:33:44:55" && !error);

    // connect a paired device: the row is busy until BtConnect answers, then the list is read again
    assert(h.model->connect("02:00:00:00:00:02"));
    assert(h.model->op() == DeviceListModel::Op::Connect && h.model->op_message() == "Connecting...");
    assert(!h.model->connect("02:00:00:00:00:01")); /* one operation at a time */
    assert(h.service.next() == "BtConnect");
    h.service.answer("BtConnect", 0, "ok");
    assert(h.model->op() == DeviceListModel::Op::None);
    assert(h.model->result() == "Connected to Studio Headphones" && !h.model->result_failed());
    assert(h.model->status_line(error) == "Connected to Studio Headphones" && !error);
    assert(h.ended.back() == std::make_pair(DeviceListModel::Op::Connect, true));
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));

    // a failed disconnect: the backend's reason in the status line (red), the agent session reset
    h.agent.accepted("RequestConfirmation", "/org/bluez/hci0/dev_02_00_00_00_00_01", false);
    assert(h.model->disconnect("02:00:00:00:00:01"));
    assert(h.agent.pairing_device().empty()); /* a new operation: no earlier decision carries over */
    h.agent.accepted("RequestConfirmation", "/org/bluez/hci0/dev_02_00_00_00_00_01", false);
    h.service.answer("BtDisconnect", -1, "Not Connected");
    assert(h.model->result() == "Bluetooth action failed: Not Connected" && h.model->result_failed());
    assert(h.model->status_line(error) == "Bluetooth action failed: Not Connected" && error);
    assert(h.agent.pairing_device().empty());
    assert(h.ended.back() == std::make_pair(DeviceListModel::Op::Disconnect, false));
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));

    // forget, rename
    assert(h.model->forget("02:00:00:00:00:02") && h.model->op_message() == "Removing...");
    assert(h.service.log.back() == "BtRemove 02:00:00:00:00:02");
    h.service.answer("BtRemove", 0);
    assert(h.model->result() == "Forgot Studio Headphones");
    h.service.answer("BtList", 1, device_lines({kDevices[0]}));
    assert(h.model->devices().size() == 1);
    assert(!h.model->rename("02:00:00:00:00:01", "")); /* refused before anything is sent */
    assert(h.model->result() == "Name must be 1-63 UTF-8 bytes." && h.model->result_failed());
    assert(h.model->rename("02:00:00:00:00:01", "Desk keys"));
    assert(h.service.log.back() == "BtDeviceAlias 02:00:00:00:00:01 Desk keys");
    h.service.answer("BtDeviceAlias", 0);
    assert(h.model->result() == "Renamed to Desk keys");
    h.service.answer("BtList", 1, device_lines({kDevices[0]}));

    // a list reply for an older generation is dropped (an operation started meanwhile)
    h.model->refresh();
    assert(h.service.next() == "BtList");
    assert(h.model->connect("02:00:00:00:00:01"));
    h.service.answer("BtList", 0, "");
    assert(h.model->devices().size() == 1); /* the stale empty list did not replace it */
    h.service.answer("BtConnect", 0);
    h.service.answer("BtList", 1, device_lines({kDevices[0]}));
    assert(h.service.pending.empty());

    // a bad list
    h.model->refresh();
    h.service.answer("BtList", 3, "garbage");
    assert(h.model->devices().empty() && h.model->error() == "Bluetooth device list unavailable.");
    assert(h.model->status_line(error) == "Bluetooth device list unavailable." && error);
    assert(h.model->leave());
    assert(h.service.pending.empty()); /* nothing to stop on Paired Devices */
}

void test_status_states()
{
    {
        ListHarness h(ListMode::Paired);
        h.model->open();
        h.service.answer("BtStatus", -1, "no adapter");
        assert(!h.model->status_known() && h.model->error() == "Bluetooth service unavailable.");
        h.model->retry_status();
        assert(h.service.next() == "BtStatus");
        h.model->retry_status(); /* already asking */
        assert(h.service.pending.size() == 1);
        // BlueZ up but the adapter not ready yet: retrying, not "off"
        h.service.answer("BtStatus", 0, "0\t\t0\t");
        assert(!h.model->power_off() && h.model->error() == "Bluetooth adapter unavailable. Retrying...");
        h.model->retry_status();
        h.service.answer("BtStatus", 0, kStatusOff);
        assert(h.model->power_off() && h.model->error().empty());
        bool error = false;
        assert(h.model->status_line(error) == "Bluetooth is off");
        assert(h.service.pending.empty()); /* no list while off */
        assert(!h.model->connect("02:00:00:00:00:01"));
        h.model->retry_status();
        assert(h.service.pending.empty()); /* a real "off" does not spin */
    }
}

void test_scan()
{
    ListHarness h(ListMode::Scan);
    h.model->open();
    assert(h.model->scanning());
    h.service.answer("BtStatus", 0, kStatusOn);
    // nothing to stop yet: discovery starts at once, then the list
    assert(h.service.next() == "BtDiscoveryStart");
    assert(h.model->loading());
    h.service.answer("BtDiscoveryStart", 0);
    assert(h.model->discovering());
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));
    assert(h.model->devices().size() == 3); /* Named Only: the unnamed ones are hidden */
    h.model->scan_tick();
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));
    h.named_only = false;
    h.model->scan_tick();
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));
    assert(h.model->devices().size() == 5);
    h.named_only = true;

    // pair a new device: discovery stops first, then Pair, then Connect, then a fresh discovery
    h.service.log.clear();
    assert(h.model->pair("02:00:00:00:00:03"));
    assert(h.model->op() == DeviceListModel::Op::Pair && h.model->op_message() == "Pairing...");
    h.service.answer("BtDiscoveryStop", 0);
    h.service.answer("BtPair", 0);
    assert(h.model->op_message() == "Connecting...");
    h.service.answer("BtConnect", 0);
    assert(h.model->result() == "Paired with Field Phone");
    assert(h.ended.back() == std::make_pair(DeviceListModel::Op::Pair, true));
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", 0);
    h.service.answer("BtList", 0, "");
    assert((h.service.log == std::vector<std::string>{"BtDiscoveryStop", "BtPair 02:00:00:00:00:03",
                                                      "BtConnect 02:00:00:00:00:03", "BtStatus", "BtDiscoveryStart",
                                                      "BtList 16"}));
    assert(h.service.pending.empty());
    assert(h.model->leave());
    assert(h.service.next() == "BtDiscoveryStop"); /* discovery does not outlive the page */
}

void test_scan_pair_failure_and_leave()
{
    ListHarness h(ListMode::Scan);
    h.model->open();
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", 0);
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));
    h.service.log.clear();
    assert(h.model->pair("02:00:00:00:00:03"));
    h.service.answer("BtDiscoveryStop", 0);
    h.service.answer("BtPair", -1, "Authentication Rejected");
    assert(h.model->cleanup_pending() && h.model->op_message() == "Cleaning up pairing...");
    assert(!h.model->leave()); /* the temporary device must go first */
    h.service.answer("BtCancelPairing", 0);
    h.service.answer("BtRemove", 0);
    assert(!h.model->cleanup_pending());
    assert(h.model->result() == "Bluetooth action failed: Authentication Rejected");
    assert(h.ended.back() == std::make_pair(DeviceListModel::Op::Pair, false));
    assert((h.service.log == std::vector<std::string>{"BtDiscoveryStop", "BtPair 02:00:00:00:00:03",
                                                      "BtCancelPairing 02:00:00:00:00:03",
                                                      "BtRemove 02:00:00:00:00:03", "BtStatus"}));
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", 0);
    h.service.answer("BtList", static_cast<int>(kDevices.size()), device_lines(kDevices));

    // leaving while a pairing is in flight cancels it in BlueZ; replies after that change nothing
    h.service.log.clear();
    assert(h.model->pair("02:00:00:00:00:03"));
    h.service.answer("BtDiscoveryStop", 0);
    assert(h.service.next() == "BtPair");
    assert(h.model->leave());
    assert(h.model->leaving() && h.model->op() == DeviceListModel::Op::None);
    assert(h.service.log.back() == "BtCancelPairing 02:00:00:00:00:03");
    h.service.answer("BtPair", -1, "Canceled");
    h.service.answer("BtCancelPairing", 0);
    assert(h.service.pending.empty());
}

void test_scan_toggle()
{
    ListHarness h(ListMode::Scan);
    h.model->open();
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", 0);
    h.service.answer("BtList", 1, device_lines({kDevices[2]}));
    // off: discovery stops, the list stays, no refresh
    h.model->set_scanning(false);
    assert(!h.model->scanning());
    h.service.answer("BtDiscoveryStop", 0);
    assert(h.model->devices().size() == 1);
    h.model->scan_tick();
    assert(h.service.pending.empty());
    // on: a fresh discovery (status, start) and an empty list until it reports
    h.model->set_scanning(true);
    assert(h.model->scanning() && h.model->devices().empty());
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", 0);
    h.service.answer("BtList", 1, device_lines({kDevices[2]}));
    // a failed start: the toggle goes off with the reason
    h.model->set_scanning(false);
    h.service.answer("BtDiscoveryStop", 0);
    h.model->set_scanning(true);
    h.service.answer("BtStatus", 0, kStatusOn);
    h.service.answer("BtDiscoveryStart", -1, "Busy");
    assert(!h.model->scanning() && h.model->error() == "Bluetooth scan unavailable.");
    // stopping before the start answered: the stop follows the start
    h.model->set_scanning(true);
    h.service.answer("BtStatus", 0, kStatusOn);
    assert(h.service.next() == "BtDiscoveryStop"); /* the failed start may still own a scan session */
    h.service.answer("BtDiscoveryStop", 0);
    assert(h.service.next() == "BtDiscoveryStart");
    h.model->set_scanning(false);
    h.service.answer("BtDiscoveryStart", 0);
    assert(h.service.next() == "BtDiscoveryStop");
    h.service.answer("BtDiscoveryStop", 0);
    assert(!h.model->discovering() && h.service.pending.empty());
    assert(h.model->leave());
}

void test_alias()
{
    FakeService service;
    std::vector<std::string> saved;
    int done = 0;
    AliasModel::Hooks hooks;
    hooks.request = service.transport();
    hooks.saved = [&](const std::string &alias) { saved.push_back(alias); };
    hooks.done = [&] { ++done; };
    {
        AliasModel model("", hooks); /* an invalid tree label: the legacy default */
        assert(model.alias() == "CardputerZero");
        model.open();
        service.answer("BtStatus", 0, kStatusOn);
        assert(model.alias() == "deck" && model.take_text() && !model.take_text());
        assert(saved == std::vector<std::string>{"deck"});
        assert(!model.save("")); /* refused before anything is sent */
        assert(model.error() == "Name must be 1-63 UTF-8 bytes.");
        model.edited("deck 2");
        assert(model.error().empty());
        assert(model.save("deck 2") && model.saving());
        assert(service.log.back() == "BtAlias deck 2");
        service.answer("BtAlias", -1, "failed");
        assert(!model.saving() && model.error() == "Set alias failed.");
        assert(model.alias() == "deck" && model.take_text()); /* the backend's name back in the field */
        assert(model.save("deck 3"));
        service.answer("BtAlias", 0, "ok");
        assert(saved.back() == "deck 3" && done == 1);
    }
    {
        AliasModel model("zero", hooks);
        model.edited("zero7"); /* typed before the status came: a late read does not replace it */
        model.open();
        service.answer("BtStatus", 0, kStatusOff);
        assert(model.alias() == "zero7" && !model.take_text());
        assert(model.power_off());
        assert(!model.save("zero7")); /* off: the page shows the legacy notice */
        assert(service.pending.empty());
    }
    {
        AliasModel model("zero", hooks);
        model.open();
        service.answer("BtStatus", -1, "");
        assert(!model.status_known() && model.error() == "Bluetooth service unavailable.");
        assert(!model.save("zero"));
        model.retry_status();
        service.answer("BtStatus", 0, kStatusOn);
        assert(model.error().empty() && model.status_known());
    }
    {
        // a reply after the page went is dropped
        auto model = std::make_unique<AliasModel>("zero", hooks);
        model->open();
        model.reset();
        service.answer("BtStatus", 0, kStatusOn);
    }
}

} // namespace

int main()
{
    test_filter_and_texts();
    test_prompt_rules();
    test_agent_session();
    test_paired_list();
    test_status_states();
    test_scan();
    test_scan_pair_failure_and_leave();
    test_scan_toggle();
    test_alias();
    std::puts("settings bluetooth model: ok");
    return 0;
}
