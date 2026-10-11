/*
 * SPDX-License-Identifier: MIT
 *
 * The native Wi-Fi page (see settings_native_wifi_page.hpp).
 *
 * The service calls are the legacy page's (settings_wifi_com) and run on a worker (the legacy component base's async
 * tasks; results on the LVGL thread, every result checked against a generation so a dropped operation can never
 * paint). The scan / connect / forget rules are the legacy page's (settings_wifi_page.cpp): a scan result keeps a
 * just-forgotten network out of the list, a connection only counts when the status then names the network, Forget
 * disconnects the network first when it is the active one.
 */

#include "settings_native_wifi_page.hpp"

#include "lvgl_components.hpp"
#include "settings_page.hpp"
#include "settings_system_model.hpp"
#include "settings_wifi_model.hpp"

#include "../launcher_toast.h"

#include "cp0_ui_widgets.hpp"
#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using settings_wifi::AccessPoint;
using settings_wifi::Status;
using Callbacks = DComponens::LvglComponensBase;

constexpr uint32_t kRefreshMs = 8000;               /* the legacy page's list refresh while it is shown */
constexpr std::chrono::milliseconds kNoTimeout{-1}; /* the service has its own timeouts */

class Tasks : public DComponens::LvglComponensBase {
public:
    void create_ui(lv_obj_t *) override {}
    void LoadNextPage() override {}
    void LeaveNextPage() override {}
};

enum class Op { None, Toggle, Connect, Forget, Disconnect };
enum class Origin { List, Password, Hidden }; /* where a connection was asked from */

struct OpRequest {
    Op op = Op::None;
    Origin origin = Origin::List;
    std::string ssid;
    std::string password;
    std::string security;
    bool saved = false;
    bool active = false; /* Forget: the network is the connected one */
};

struct OpReply {
    int code = CP0_WIFI_ERROR_SERVICE;
    Status status;
    bool status_valid = false;
    bool radio_on = false;
};

struct ScanReply {
    int count = CP0_WIFI_ERROR_SERVICE;
    std::vector<AccessPoint> networks;
    Status status;
    bool status_valid = false;
};

struct NetInfoReply {
    int code = -1;
    std::string payload;
};

bool status_names(const Status &status, const std::string &ssid)
{
    return status.connected && status.ssid == ssid;
}

/* A Wi-Fi operation on the worker (the legacy page's connection task). */
OpReply run_op(const OpRequest &request, bool has_enable, NodeIter enable)
{
    OpReply reply;
    switch (request.op) {
    case Op::Toggle:
        // the section's Enable entry, as the legacy submenu toggles it: activate, then read the state back
        if (!has_enable) break;
        try {
            enable->Componens_api(SettingApiActivate, nullptr);
            std::atomic_bool started{false};
            SettingApiReadFlagTimeStartData result = std::make_tuple(false, &started);
            enable->Componens_api(SettingApiReadFlagTimeStart, &result);
            reply.radio_on = std::get<0>(result);
            reply.code = 0;
        } catch (...) {
        }
        break;
    case Op::Connect:
        reply.code = request.origin == Origin::Hidden
                         ? settings_wifi_com::connect_hidden(request.ssid, request.password)
                         : settings_wifi_com::connect(request.ssid, request.password);
        break;
    case Op::Forget: {
        // disconnect an active target before deleting its profile (NetworkManager races the two otherwise)
        Status before;
        const bool read_ok = settings_wifi_com::read_status(before) == 0;
        const bool disconnect = read_ok ? status_names(before, request.ssid) : request.active;
        int result = 0;
        if (disconnect) {
            result = settings_wifi_com::profile_disconnect_active();
            Status after;
            if (result != 0 && settings_wifi_com::read_status(after) == 0 && !after.connected) result = 0;
        }
        if (result == 0) {
            result = settings_wifi_com::profile_forget(request.ssid);
            if (result == CP0_WIFI_ERROR_NOT_FOUND) result = 0; /* already gone: what was asked */
        }
        reply.code = result;
        break;
    }
    case Op::Disconnect: {
        reply.code = settings_wifi_com::profile_disconnect_active();
        Status after;
        if (reply.code != 0 && settings_wifi_com::read_status(after) == 0 && !after.connected) reply.code = 0;
        break;
    }
    case Op::None:
        break;
    }
    reply.status_valid = settings_wifi_com::read_status(reply.status) == 0;
    return reply;
}

bool same_spec(const cp0::ui::RowSpec &a, const cp0::ui::RowSpec &b)
{
    return a.kind == b.kind && a.label == b.label && a.value == b.value && a.on == b.on && a.bars == b.bars &&
           a.secured == b.secured;
}

class WifiPanel : public SettingsNativePanel {
public:
    WifiPanel(const NodeIter &node, const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), title_(context.title), host_back_(context.back)
    {
        const SettingsWifiSection section = settings_wifi_section(node);
        has_enable_ = section.has_enable;
        enable_ = section.enable;
        has_hidden_ = section.has_hidden;
        hidden_label_ = section.hidden_label;
        tasks_ = std::make_unique<Tasks>();

        // the radio state now, as the legacy Networks factory reads it before the page is built
        radio_on_ = true;
        if (has_enable_) {
            bool on = false;
            try {
                enable_->Componens_api(SettingApiReadFlag, &on);
                radio_on_ = on;
            } catch (...) {
            }
        }

        cp0::ui::RollerEvents events;
        events.activate = [this](int row) { activate(row); };
        events.back = host_back_;
        roller_ = std::make_unique<cp0::ui::Roller>(page_.root(), page_.layout(), std::move(events));
        focus(roller_->focus_object());
        rebuild(0);
        show_status();
        refresh_timer_ = lv_timer_create(refresh_cb, kRefreshMs, this);
        if (radio_on_) start_scan(true);
    }

    ~WifiPanel() override
    {
        lv_async_call_cancel(run_posted_cb, this);
        posted_.clear();
        if (refresh_timer_) lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
        fields_.clear(); /* they stop editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        details_.reset();
        tasks_.reset(); /* cancels and joins the workers: no result can arrive after this */
        roller_.reset();
        page_.set_caption("", false);
    }

    bool back() override
    {
        if (dialog_) {
            cancel_dialog();
            return true;
        }
        if (level_ == Level::Details) {
            close_details();
            set_level(Level::Actions, 0);
            return true;
        }
        if (level_ == Level::Actions) {
            set_level(Level::List, list_row_of(action_ssid_)); /* the highlight back on the network */
            return true;
        }
        return false;
    }

private:
    enum class Level { List, Actions, Details };
    enum class RowType { Toggle, Network, Placeholder, Hidden, Rescan, Details, Forget, Disconnect };
    enum class DialogKind { None, Password, Hidden };

    struct Row {
        RowType type;
        std::string ssid; /* Network */
    };

    static std::string key_of(const Row &row)
    {
        return std::to_string(static_cast<int>(row.type)) + ":" + row.ssid;
    }

    // ---------------------------------------------------------------- posting (out of the widgets' own events)

    void post(std::function<void()> callback)
    {
        posted_.push_back(std::move(callback));
        if (!post_armed_) post_armed_ = lv_async_call(run_posted_cb, this) == LV_RESULT_OK;
    }

    static void run_posted_cb(void *data)
    {
        auto *self = static_cast<WifiPanel *>(data);
        if (!self) return;
        self->post_armed_ = false;
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(self->posted_);
        for (auto &callback : callbacks) callback();
    }

    void focus(lv_obj_t *obj)
    {
        if (!group_ || !obj) return;
        if (lv_obj_get_group(obj) != group_) lv_group_add_obj(group_, obj);
        lv_group_focus_obj(obj);
    }

    void unfocus(lv_obj_t *obj)
    {
        if (obj && lv_obj_get_group(obj)) lv_group_remove_obj(obj);
    }

    // ---------------------------------------------------------------- rows

    const AccessPoint *network(const std::string &ssid) const
    {
        for (const AccessPoint &ap : networks_)
            if (ap.ssid == ssid) return &ap;
        return nullptr;
    }

    /* The list row of a network (the toggle is row 0), or 0 when it is not listed. */
    int list_row_of(const std::string &ssid) const
    {
        if (!radio_on_ || op_ == Op::Toggle) return 0;
        for (std::size_t i = 0; i < networks_.size(); ++i)
            if (networks_[i].ssid == ssid) return static_cast<int>(i) + 1;
        return 0;
    }

    cp0::ui::RowSpec network_spec(const AccessPoint &ap) const
    {
        cp0::ui::RowSpec spec;
        if (op_ != Op::None && op_ != Op::Toggle && op_ssid_ == ap.ssid) {
            spec.kind = cp0::ui::RowKind::Busy;
            spec.label = op_ == Op::Connect ? "Connecting..." : op_ == Op::Forget ? "Forgetting..." : "Disconnecting...";
            return spec;
        }
        spec.kind = cp0::ui::RowKind::Network;
        spec.label = ap.ssid;
        spec.on = settings_wifi::is_connected(ap, status_);
        spec.bars = settings_wifi::signal_bars(ap.signal);
        spec.secured = !settings_wifi_com::is_open_security(ap.security);
        if (ap.saved && !spec.on) spec.value = "Saved";
        return spec;
    }

    void list_rows(std::vector<Row> &rows, std::vector<cp0::ui::RowSpec> &specs) const
    {
        cp0::ui::RowSpec toggle;
        toggle.kind = cp0::ui::RowKind::Toggle;
        toggle.label = "Wi-Fi";
        toggle.on = op_ == Op::Toggle ? !radio_on_ : radio_on_; /* the asked state while it switches */
        rows.push_back({RowType::Toggle, {}});
        specs.push_back(toggle);
        if (!radio_on_ || op_ == Op::Toggle) return;
        for (const AccessPoint &ap : networks_) {
            rows.push_back({RowType::Network, ap.ssid});
            specs.push_back(network_spec(ap));
        }
        if (networks_.empty()) {
            cp0::ui::RowSpec placeholder;
            if (scanning_) {
                placeholder.kind = cp0::ui::RowKind::Busy;
                placeholder.label = "Scanning...";
            } else {
                placeholder.label = scan_error_.empty() ? "No networks found" : scan_error_;
            }
            rows.push_back({RowType::Placeholder, {}});
            specs.push_back(placeholder);
        }
        if (has_hidden_) {
            cp0::ui::RowSpec hidden;
            hidden.label = hidden_label_;
            rows.push_back({RowType::Hidden, {}});
            specs.push_back(hidden);
        }
        cp0::ui::RowSpec rescan;
        rescan.label = "Rescan";
        if (scanning_ && scan_user_ && !networks_.empty()) rescan.value = "Scanning...";
        rows.push_back({RowType::Rescan, {}});
        specs.push_back(rescan);
    }

    void action_rows(std::vector<Row> &rows, std::vector<cp0::ui::RowSpec> &specs) const
    {
        for (const auto &entry : {std::make_pair(RowType::Details, "Details"), std::make_pair(RowType::Forget, "Forget"),
                                  std::make_pair(RowType::Disconnect, "Disconnect")}) {
            cp0::ui::RowSpec spec;
            spec.label = entry.second;
            rows.push_back({entry.first, action_ssid_});
            specs.push_back(spec);
        }
    }

    /* The roller's rows for the level; rows that kept their kind change in place (the highlight and the scroll stay),
     * otherwise the list is rebuilt with the highlight on the same row (selected >= 0: on that row instead). */
    void rebuild(int selected = -1)
    {
        if (!roller_ || level_ == Level::Details) return;
        std::vector<Row> rows;
        std::vector<cp0::ui::RowSpec> specs;
        if (level_ == Level::List) list_rows(rows, specs);
        else action_rows(rows, specs);

        bool same = selected < 0 && rows.size() == rows_.size();
        for (std::size_t i = 0; same && i < rows.size(); ++i)
            same = specs[i].kind == specs_[i].kind && key_of(rows[i]) == key_of(rows_[i]);
        if (same) {
            for (std::size_t i = 0; i < rows.size(); ++i)
                if (!same_spec(specs[i], specs_[i])) roller_->update_row(static_cast<int>(i), specs[i]);
        } else {
            int target = selected;
            if (target < 0) {
                const int current = roller_->selected();
                target = std::min(current, static_cast<int>(rows.size()) - 1);
                if (current >= 0 && current < static_cast<int>(rows_.size()))
                    for (std::size_t i = 0; i < rows.size(); ++i)
                        if (key_of(rows[i]) == key_of(rows_[static_cast<std::size_t>(current)]))
                            target = static_cast<int>(i);
            }
            roller_->set_rows(specs, std::max(0, target), true);
        }
        rows_ = std::move(rows);
        specs_ = std::move(specs);
    }

    void set_level(Level level, int selected)
    {
        level_ = level;
        std::vector<std::string> title = title_;
        if (level_ != Level::List) title.push_back(action_ssid_);
        if (level_ == Level::Details) title.push_back("Details");
        page_.set_title(title);
        if (level_ != Level::Details) rebuild(selected);
    }

    void show_status()
    {
        if (!error_.empty()) page_.set_caption(error_, true);
        else page_.set_caption(settings_wifi::status_line(status_, radio_on_), false);
        if (dialog_) dialog_->raise();
    }

    void fail(const std::string &text)
    {
        error_ = text;
        launcher_toast().show(text.c_str());
    }

    // ---------------------------------------------------------------- activation

    void activate(int index)
    {
        if (index < 0 || index >= static_cast<int>(rows_.size()) || dialog_) return;
        const Row row = rows_[static_cast<std::size_t>(index)];
        error_.clear();
        switch (row.type) {
        case RowType::Toggle:
            if (op_ == Op::None) start_op({Op::Toggle, Origin::List, {}, {}, {}, false, false});
            break;
        case RowType::Network: {
            const AccessPoint *ap = network(row.ssid);
            if (!ap || op_ != Op::None) break;
            switch (settings_wifi::row_action(*ap, status_)) {
            case settings_wifi::RowAction::Actions:
                action_ssid_ = ap->ssid;
                set_level(Level::Actions, 0);
                break;
            case settings_wifi::RowAction::Connect:
                start_op({Op::Connect, Origin::List, ap->ssid, {}, ap->security, ap->saved, false});
                break;
            case settings_wifi::RowAction::AskPassword:
                open_password(ap->ssid, ap->security, {});
                break;
            }
            break;
        }
        case RowType::Hidden:
            if (op_ == Op::None) open_hidden();
            break;
        case RowType::Rescan:
            start_scan(true);
            break;
        case RowType::Details:
            open_details();
            break;
        case RowType::Forget:
        case RowType::Disconnect:
            if (op_ != Op::None) break;
            {
                const AccessPoint *ap = network(row.ssid);
                OpRequest request{row.type == RowType::Forget ? Op::Forget : Op::Disconnect, Origin::List, row.ssid,
                                  {}, ap ? ap->security : std::string(), ap && ap->saved,
                                  status_names(status_, row.ssid)};
                set_level(Level::List, list_row_of(row.ssid)); /* its row shows the operation */
                start_op(std::move(request));
            }
            break;
        case RowType::Placeholder:
            break;
        }
        show_status();
    }

    // ---------------------------------------------------------------- scanning

    static void refresh_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<WifiPanel *>(lv_timer_get_user_data(timer));
        // the list keeps current while it is shown; an operation owns the device until it ends
        if (self && self->level_ == Level::List && self->op_ == Op::None && !self->dialog_ && self->radio_on_)
            self->start_scan(false);
    }

    /* user: asked for (Rescan, the page opening, after an operation): the Rescan row says "Scanning..." meanwhile */
    void start_scan(bool user)
    {
        if (!radio_on_ || op_ != Op::None) return;
        if (scanning_) {
            scan_again_ = true;
            scan_user_ = scan_user_ || user;
            return;
        }
        scanning_ = true;
        scan_user_ = user;
        const std::uint64_t generation = ++scan_generation_;
        Callbacks::AsyncTaskCallbacks<ScanReply> callbacks;
        callbacks.execute = [] {
            ScanReply reply;
            std::vector<AccessPoint> networks;
            reply.count = settings_wifi_com::scan(networks, CP0_WIFI_AP_MAX);
            if (reply.count >= 0) reply.networks = std::move(networks);
            reply.status_valid = settings_wifi_com::read_status(reply.status) == 0;
            return reply;
        };
        callbacks.on_complete = [this, generation](Callbacks::AsyncTaskContext &, const ScanReply &reply) {
            if (generation == scan_generation_) scan_done(reply);
        };
        callbacks.on_exception = [this, generation](Callbacks::AsyncTaskContext &, std::exception_ptr) {
            if (generation == scan_generation_) scan_done(ScanReply{});
        };
        callbacks.on_schedule_failed = [this, generation](Callbacks::AsyncTaskContext &) {
            if (generation == scan_generation_) scan_done(ScanReply{});
        };
        Callbacks::AsyncTaskOptions options;
        options.timeout = kNoTimeout;
        tasks_->run_async_task(std::move(callbacks), options);
        if (scanning_ && user) rebuild();
    }

    void drop_scan()
    {
        ++scan_generation_;
        scanning_ = false;
        scan_again_ = false;
        scan_user_ = false;
    }

    /* The legacy page's apply_scan_result. */
    void scan_done(const ScanReply &reply)
    {
        scanning_ = false;
        if (reply.status_valid) {
            status_ = reply.status;
            if (!forgotten_ssid_.empty()) {
                if (reply.status.ssid == forgotten_ssid_) status_ = Status{};
                else if (reply.status.connected && !reply.status.ssid.empty()) forgotten_ssid_.clear();
            }
        }
        if (reply.count < 0) {
            networks_.clear();
            scan_error_ = settings_wifi::scan_error_text(reply.count);
            if (reply.count == CP0_WIFI_ERROR_RADIO_OFF) radio_on_ = false;
        } else {
            networks_ = reply.networks;
            for (AccessPoint &ap : networks_)
                if (!forgotten_ssid_.empty() && ap.ssid == forgotten_ssid_) ap.in_use = ap.saved = false;
            settings_wifi::sort_networks(networks_, status_);
            scan_error_.clear();
        }
        const bool again = scan_again_;
        const bool user = scan_user_;
        scan_again_ = false;
        scan_user_ = false;
        rebuild();
        show_status();
        if (again) start_scan(user);
    }

    // ---------------------------------------------------------------- operations

    void start_op(OpRequest request)
    {
        if (op_ != Op::None) return;
        drop_scan(); /* the operation owns the device; a scan in flight is not shown */
        if (request.op == Op::Connect && forgotten_ssid_ == request.ssid) forgotten_ssid_.clear();
        op_ = request.op;
        op_ssid_ = request.ssid;
        const std::uint64_t generation = ++op_generation_;
        Callbacks::AsyncTaskCallbacks<OpReply> callbacks;
        callbacks.execute = [request, has_enable = has_enable_, enable = enable_] {
            return run_op(request, has_enable, enable);
        };
        callbacks.on_complete = [this, generation, request](Callbacks::AsyncTaskContext &, const OpReply &reply) {
            if (generation == op_generation_) op_done(request, reply);
        };
        callbacks.on_exception = [this, generation, request](Callbacks::AsyncTaskContext &, std::exception_ptr) {
            if (generation == op_generation_) op_done(request, OpReply{});
        };
        callbacks.on_schedule_failed = [this, generation, request](Callbacks::AsyncTaskContext &) {
            if (generation == op_generation_) op_done(request, OpReply{});
        };
        Callbacks::AsyncTaskOptions options;
        options.timeout = kNoTimeout;
        tasks_->run_async_task(std::move(callbacks), options);
        if (op_ != Op::None) rebuild();
    }

    /* An operation in flight no longer matters (its dialog was cancelled): its result is dropped. */
    void drop_op()
    {
        ++op_generation_;
        op_ = Op::None;
        op_ssid_.clear();
    }

    /* The legacy page's process_connection_result. */
    void op_done(const OpRequest &request, const OpReply &reply)
    {
        op_ = Op::None;
        op_ssid_.clear();
        int result = reply.code;
        if (request.op == Op::Connect && result == 0 && !(reply.status_valid && status_names(reply.status, request.ssid)))
            result = reply.status_valid ? CP0_WIFI_ERROR_IP_CONFIG : CP0_WIFI_ERROR_SERVICE;
        if (reply.status_valid) status_ = reply.status;

        switch (request.op) {
        case Op::Toggle:
            if (result == 0) radio_on_ = reply.radio_on;
            if (!radio_on_) {
                networks_.clear();
                drop_scan();
            }
            break;
        case Op::Connect:
            if (result == 0) {
                if (dialog_) close_dialog();
                launcher_toast().show(("Connected to " + request.ssid).c_str());
            } else if (request.origin != Origin::List) {
                dialog_failed(settings_wifi::password_result_text(result));
            } else if (request.saved && result == CP0_WIFI_ERROR_AUTH) {
                // the saved password no longer works: ask for it, as the legacy page does
                open_password(request.ssid, request.security, settings_wifi::password_result_text(result));
            } else {
                fail(settings_wifi::connection_error_text(result));
            }
            break;
        case Op::Forget:
            if (result == 0) {
                forgotten_ssid_ = request.ssid;
                launcher_toast().show(("Forgot " + request.ssid).c_str());
                if (status_.ssid == request.ssid) status_ = Status{};
            } else {
                fail(settings_wifi::connection_error_text(result));
            }
            networks_.clear(); /* the old list must not show the forgotten network while the new scan runs */
            break;
        case Op::Disconnect:
            if (result == 0) launcher_toast().show("Disconnected");
            else fail(settings_wifi::connection_error_text(result));
            break;
        case Op::None:
            break;
        }
        rebuild();
        show_status();
        if (radio_on_ && !dialog_) start_scan(true);
    }

    // ---------------------------------------------------------------- dialogs

    void add_field(const char *label, cp0::ui::TextType type, std::size_t max_bytes)
    {
        cp0::ui::TextFieldSpec spec;
        spec.label = label;
        spec.type = type;
        spec.max_bytes = max_bytes;
        cp0::ui::TextFieldEvents events;
        events.submit = [this](const std::string &) { post([this] { submit_dialog(); }); }; /* Enter: Connect */
        events.escape = [this] { post([this] { cancel_dialog(); }); };                        /* Esc: Cancel */
        events.back = events.escape;
        fields_.push_back(
            std::make_unique<cp0::ui::TextField>(dialog_->body(), dialog_->field_layout(), spec, std::move(events)));
    }

    void open_dialog(DialogKind kind, const std::string &title)
    {
        close_dialog();
        dialog_kind_ = kind;
        cp0::ui::DialogSpec spec;
        spec.title = title;
        spec.message_line = true;
        spec.page = &page_; /* small screens with the keyboard up: the title in the header (cp0_ui_dialog.h) */
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { cancel_dialog(); }); }},
                        {"Connect", cp0::ui::ButtonRole::Primary, [this] { post([this] { submit_dialog(); }); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
    }

    void enter_dialog()
    {
        dialog_->relayout();
        unfocus(roller_->focus_object()); /* Up / Down / Tab move between the dialog's fields only */
        for (const auto &field : fields_)
            if (group_) lv_group_add_obj(group_, field->focus_object());
        if (!fields_.empty()) focus(fields_.front()->focus_object()); /* it edits: the keyboard comes */
    }

    void open_password(const std::string &ssid, const std::string &security, const std::string &message)
    {
        open_dialog(DialogKind::Password, ssid);
        dialog_ssid_ = ssid;
        dialog_security_ = security;
        add_field("Password", cp0::ui::TextType::Password, settings_wifi::kMaxPasswordBytes);
        enter_dialog();
        if (!message.empty()) dialog_->set_message(message, true);
    }

    void open_hidden()
    {
        open_dialog(DialogKind::Hidden, hidden_label_);
        dialog_ssid_.clear();
        dialog_security_ = "WPA2"; /* the legacy form's assumption */
        add_field("SSID", cp0::ui::TextType::Text, settings_wifi::kMaxSsidBytes);
        add_field("Password", cp0::ui::TextType::Password, settings_wifi::kMaxPasswordBytes);
        enter_dialog();
    }

    void close_dialog()
    {
        if (!dialog_) return;
        fields_.clear(); /* they stop editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        dialog_kind_ = DialogKind::None;
        dialog_busy_ = false;
        dialog_ssid_.clear();
        dialog_security_.clear();
        if (level_ == Level::Details && details_) focus(details_->focus_object());
        else if (roller_) focus(roller_->focus_object());
    }

    void cancel_dialog()
    {
        if (dialog_busy_) drop_op(); /* the connection's result no longer matters */
        close_dialog();
        rebuild();
        show_status();
    }

    void submit_dialog()
    {
        if (!dialog_ || dialog_busy_ || fields_.empty()) return;
        OpRequest request;
        request.op = Op::Connect;
        if (dialog_kind_ == DialogKind::Password) {
            const std::string &password = fields_[0]->text();
            const std::string error = settings_wifi::password_error(password, dialog_security_);
            if (!error.empty()) {
                dialog_->set_message(error, true);
                return;
            }
            request.origin = Origin::Password;
            request.ssid = dialog_ssid_;
            request.password = password;
        } else {
            if (fields_[0]->text().empty() || fields_.size() < 2) {
                dialog_->set_message("SSID required", true);
                return;
            }
            request.origin = Origin::Hidden;
            request.ssid = fields_[0]->text();
            request.password = fields_[1]->text();
        }
        if (op_ != Op::None) return;
        request.security = dialog_security_;
        dialog_busy_ = true;
        dialog_->set_message("Connecting...", false);
        for (const auto &field : fields_) field->end_edit(); /* the keyboard goes while it connects */
        start_op(std::move(request));
    }

    /* The connection from the dialog failed: it stays, the password is cleared, the reason shows, editing again. */
    void dialog_failed(const std::string &text)
    {
        if (!dialog_ || fields_.empty()) return;
        dialog_busy_ = false;
        cp0::ui::TextField &password = *fields_.back();
        password.set_text("");
        dialog_->set_message(text, true); /* kept in view when the keyboard comes back (Dialog::relayout) */
        focus(password.focus_object());
        password.begin_edit();
    }

    // ---------------------------------------------------------------- details

    std::vector<cp0::ui::InfoItem> details_items() const
    {
        std::string signal = "--";
        if (status_names(status_, action_ssid_) && status_.signal != 0)
            signal = status_.signal < 0 ? std::to_string(status_.signal) + " dBm"
                                        : std::to_string(settings_wifi::signal_percent(status_.signal)) + "%";
        const bool here = status_names(status_, action_ssid_);
        return {{"IP", here && !status_.ip.empty() ? status_.ip : "--"},
                {"Gateway", here ? network_info_.gateway : "--"},
                {"Signal", signal},
                {"MAC", here ? network_info_.mac : "--"}};
    }

    void open_details()
    {
        roller_->set_hidden(true);
        unfocus(roller_->focus_object());
        network_info_ = settings_system::NetworkInfo{"--", "--", "--"};
        details_ = std::make_unique<cp0::ui::InfoTable>(page_.root(), page_.layout(), host_back_);
        details_->set_items(details_items());
        focus(details_->focus_object());
        set_level(Level::Details, -1);
        if (dialog_) dialog_->raise();

        // gateway and MAC: the OS info service's default route (the Ethernet info page's command)
        const std::uint64_t generation = ++details_generation_;
        Callbacks::AsyncTaskCallbacks<NetInfoReply> callbacks;
        callbacks.execute = [] {
            auto reply = std::make_shared<NetInfoReply>();
            try {
                cp0_signal_osinfo_api({"NetworkDefaultInfoRead"}, [reply](int code, std::string data) {
                    reply->code = code;
                    reply->payload = std::move(data);
                });
            } catch (...) {
            }
            return *reply;
        };
        callbacks.on_complete = [this, generation](Callbacks::AsyncTaskContext &, const NetInfoReply &reply) {
            if (generation != details_generation_ || !details_) return;
            settings_system::NetworkInfo info;
            if (reply.code == 0 && settings_system::parse_network_info_strict(reply.payload, info)) {
                network_info_ = info;
                details_->set_items(details_items());
            }
        };
        tasks_->run_async_task(std::move(callbacks));
    }

    void close_details()
    {
        ++details_generation_;
        details_.reset();
        roller_->set_hidden(false);
        focus(roller_->focus_object());
    }

    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::vector<std::string> title_;
    std::function<void()> host_back_;
    bool has_enable_ = false;
    NodeIter enable_;
    bool has_hidden_ = false;
    std::string hidden_label_;

    std::unique_ptr<Tasks> tasks_;
    std::unique_ptr<cp0::ui::Roller> roller_;
    std::unique_ptr<cp0::ui::InfoTable> details_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::vector<std::unique_ptr<cp0::ui::TextField>> fields_;
    lv_timer_t *refresh_timer_ = nullptr;
    std::vector<std::function<void()>> posted_;
    bool post_armed_ = false;

    Level level_ = Level::List;
    std::vector<Row> rows_;
    std::vector<cp0::ui::RowSpec> specs_;
    std::string action_ssid_; /* Actions / Details: the connected network */
    settings_system::NetworkInfo network_info_{"--", "--", "--"};
    std::uint64_t details_generation_ = 0;

    bool radio_on_ = false;
    Status status_;
    std::vector<AccessPoint> networks_;
    bool scanning_ = false;
    bool scan_again_ = false;
    bool scan_user_ = false;
    std::string scan_error_;
    std::string forgotten_ssid_; /* keeps a just-forgotten network out of one stale scan */
    std::uint64_t scan_generation_ = 0;
    Op op_ = Op::None;
    std::string op_ssid_;
    std::uint64_t op_generation_ = 0;
    std::string error_; /* the last failure, in the status line until the next action */

    DialogKind dialog_kind_ = DialogKind::None;
    std::string dialog_ssid_;
    std::string dialog_security_;
    bool dialog_busy_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_wifi_panel_create(const NodeIter &node,
                                                                const SettingsNativePanelContext &context)
{
    return std::make_unique<WifiPanel>(node, context);
}
