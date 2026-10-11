/*
 * SPDX-License-Identifier: MIT
 *
 * The native Bluetooth pages (see settings_native_bluetooth_page.hpp).
 *
 * The Bluetooth API is called exactly as the legacy page calls it (cp0_signal_bt_api on a worker thread, the reply
 * queued and handled on the LVGL thread by a 50 ms timer; every reply checked against the model's generation, so a
 * dropped operation can never paint). The agent prompts come the same way (cp0_signal_bt_agent on a BlueZ worker,
 * queued, shown on the LVGL thread; every request answered exactly once, also when the page goes).
 */

#include "settings_native_bluetooth_page.hpp"

#include "settings_bluetooth_model.hpp"
#include "settings_bluetooth_page.hpp" /* settings_bluetooth_named_only_enabled(), cp0::SignalRegistration */
#include "settings_page.hpp"

#include "cp0_bounded_task_registry.hpp"
#include "cp0_ui_widgets.hpp"
#include "hal_lvgl_bsp.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {

using settings_bluetooth::DeviceListModel;
using settings_bluetooth::DeviceRecord;
using settings_bluetooth::ListMode;
using settings_bluetooth::PromptKind;
using Op = DeviceListModel::Op;

constexpr uint32_t kReplyMs = 50;         /* the legacy page's result timer */
constexpr uint32_t kStatusRetryMs = 1000; /* the legacy page's status retry while BlueZ is not ready */
constexpr uint32_t kScanRefreshMs = 1500; /* the legacy page's list refresh while discovering */
constexpr std::size_t kMaxNameBytes = CP0_BT_NAME_MAX - 1;

/* ------------------------------------------------------------------ posting (out of the widgets' own events) */

class Poster {
public:
    ~Poster()
    {
        lv_async_call_cancel(run_cb, this);
        posted_.clear();
    }
    void post(std::function<void()> callback)
    {
        posted_.push_back(std::move(callback));
        if (!armed_) armed_ = lv_async_call(run_cb, this) == LV_RESULT_OK;
    }

private:
    static void run_cb(void *data)
    {
        auto *self = static_cast<Poster *>(data);
        if (!self) return;
        self->armed_ = false;
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(self->posted_);
        for (auto &callback : callbacks) callback();
    }
    std::vector<std::function<void()>> posted_;
    bool armed_ = false;
};

/* ------------------------------------------------------------------ the Bluetooth API on a worker */

class Transport {
public:
    Transport() { timer_ = lv_timer_create(drain_cb, kReplyMs, this); }
    ~Transport()
    {
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->stopped = true;
            shared_->pending.clear();
        }
        alive_.reset();
        tasks_.join_all(); /* requests already sent (a discovery stop, a pairing cancel) still reach the service */
    }
    Transport(const Transport &) = delete;
    Transport &operator=(const Transport &) = delete;

    settings_bluetooth::Transport function()
    {
        return [this](std::list<std::string> request, settings_bluetooth::Reply reply) {
            send(std::move(request), std::move(reply));
        };
    }

private:
    struct Item {
        settings_bluetooth::Reply reply;
        int code = -1;
        std::string data;
    };
    struct Shared {
        std::mutex mutex;
        bool stopped = false;
        std::deque<Item> pending;
    };

    void send(std::list<std::string> request, settings_bluetooth::Reply reply)
    {
        const std::shared_ptr<Shared> shared = shared_;
        auto deliver = [shared, reply](int code, std::string data) {
            try {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (!shared->stopped) shared->pending.push_back({reply, code, std::move(data)});
            } catch (...) {
            }
        };
        const bool started = tasks_.start([request = std::move(request), deliver]() mutable {
            try {
                cp0_signal_bt_api(std::move(request), deliver);
            } catch (...) {
                deliver(-1, "Bluetooth service unavailable");
            }
        });
        if (!started) deliver(-1, "Bluetooth request could not be scheduled");
    }

    static void drain_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<Transport *>(lv_timer_get_user_data(timer));
        if (!self) return;
        self->tasks_.reap_finished();
        std::deque<Item> items;
        {
            std::lock_guard<std::mutex> lock(self->shared_->mutex);
            items.swap(self->shared_->pending);
        }
        const std::weak_ptr<bool> alive = self->alive_;
        for (Item &item : items) {
            if (alive.expired()) return;
            if (item.reply) item.reply(item.code, std::move(item.data));
        }
    }

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    Cp0BoundedTaskRegistry tasks_;
    lv_timer_t *timer_ = nullptr;
};

/* ------------------------------------------------------------------ helpers */

void focus_in(lv_group_t *group, lv_obj_t *obj)
{
    if (!group || !obj) return;
    if (lv_obj_get_group(obj) != group) lv_group_add_obj(group, obj);
    lv_group_focus_obj(obj);
}

void unfocus(lv_obj_t *obj)
{
    if (obj && lv_obj_get_group(obj)) lv_group_remove_obj(obj);
}

bool same_spec(const cp0::ui::RowSpec &a, const cp0::ui::RowSpec &b)
{
    return a.kind == b.kind && a.label == b.label && a.value == b.value && a.on == b.on;
}

/* A notice with one OK (Enter and Esc press it): the legacy power warning. */
std::unique_ptr<cp0::ui::Dialog> notice(cp0::ui::Page &page, const std::string &title, const std::string &text,
                                        std::function<void()> ok)
{
    cp0::ui::DialogSpec spec;
    spec.title = title;
    spec.page = &page;
    spec.buttons = {{"OK", cp0::ui::ButtonRole::Primary, std::move(ok)}};
    auto dialog = std::make_unique<cp0::ui::Dialog>(page.root(), page.layout(), std::move(spec));
    dialog->add_text(text);
    dialog->relayout();
    return dialog;
}

/* ------------------------------------------------------------------ BlueZ agent prompts */

/* The agent's requests as Dialogs over the page that shows (the legacy page's rules: settings_bluetooth::AgentSession,
 * prompt_kind, agent_input_error). Subscribed while it exists; every request is answered exactly once. */
class AgentPrompts {
public:
    struct Context {
        cp0::ui::Page &page;
        lv_group_t *group;
        std::function<bool()> operation_pending;      /* an operation the user asked for is running */
        std::function<std::string()> operation_address; /* its device ("" when none) */
        std::function<std::string(const std::string &address)> name_of; /* a listed device's label ("" unknown) */
    };

    explicit AgentPrompts(Context context) : context_(std::move(context))
    {
        timer_ = lv_timer_create(drain_cb, kReplyMs, this);
        const std::shared_ptr<Shared> shared = shared_;
        registration_.replace(cp0_signal_bt_agent, [shared](uint64_t id, std::string method, std::string device,
                                                            std::string value,
                                                            std::function<void(bool, std::string)> reply) {
            if (!reply) return;
            bool queued = false;
            try {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (!shared->stopped) {
                    shared->queue.push_back({id, std::move(method), std::move(device), std::move(value), reply});
                    queued = true;
                }
            } catch (...) {
            }
            if (!queued) {
                try {
                    reply(false, {});
                } catch (...) {
                }
            }
        });
    }

    ~AgentPrompts()
    {
        registration_.reset();
        if (timer_) lv_timer_delete(timer_);
        timer_ = nullptr;
        std::deque<Request> queued;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->stopped = true;
            queued.swap(shared_->queue);
        }
        for (Request &request : queued) answer(request, false, {});
        if (dialog_) {
            Request current = std::move(current_);
            close();
            answer(current, false, {});
        }
    }
    AgentPrompts(const AgentPrompts &) = delete;
    AgentPrompts &operator=(const AgentPrompts &) = delete;

    settings_bluetooth::AgentSession &session() { return session_; }
    bool showing() const { return dialog_ != nullptr; }

    /* Esc (a key on the page, the header's Esc): the shown request is rejected. */
    bool back()
    {
        if (!dialog_) return false;
        reject();
        return true;
    }

    /* An operation ended. The pairing's end takes a code shown for it away; a failure also every prompt (BlueZ
     * cancelled the agent's requests). */
    void operation_ended(bool pairing, bool ok)
    {
        if (!dialog_) return;
        if (settings_bluetooth::prompt_is_display(kind_) && pairing) {
            close();
            return;
        }
        if (pairing && !ok) reject();
    }

    void raise()
    {
        if (dialog_) dialog_->raise();
    }

    /* The page changed under the prompt (its own dialog came or went): the focus goes there when the prompt closes. */
    void set_return_focus(lv_obj_t *obj) { previous_focus_ = obj; }

private:
    struct Request {
        uint64_t id = 0;
        std::string method;
        std::string device;
        std::string value;
        std::function<void(bool, std::string)> reply;
    };
    struct Shared {
        std::mutex mutex;
        bool stopped = false;
        std::deque<Request> queue;
    };

    static void answer(Request &request, bool accepted, const std::string &text)
    {
        if (!request.reply) return;
        auto reply = std::move(request.reply);
        request.reply = nullptr;
        try {
            reply(accepted, accepted ? text : std::string());
        } catch (...) {
        }
    }

    static void drain_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<AgentPrompts *>(lv_timer_get_user_data(timer));
        if (!self) return;
        std::deque<Request> requests;
        {
            std::lock_guard<std::mutex> lock(self->shared_->mutex);
            requests.swap(self->shared_->queue);
        }
        for (Request &request : requests) self->show(std::move(request));
    }

    std::string request_device(const Request &request) const
    {
        if (!request.device.empty()) return request.device;
        return context_.operation_address ? context_.operation_address() : std::string();
    }

    /* The device's name, else its address, else the request's object path. */
    std::string device_title(const Request &request) const
    {
        std::string address = settings_bluetooth::address_of_path(request.device);
        if (address.empty() && request.device.empty() && context_.operation_address)
            address = context_.operation_address();
        if (address.empty()) return request.device;
        const std::string name = context_.name_of ? context_.name_of(address) : std::string();
        return name.empty() ? address : name;
    }

    std::string device_line(const Request &request) const
    {
        std::string address = settings_bluetooth::address_of_path(request.device);
        if (address.empty() && request.device.empty() && context_.operation_address)
            address = context_.operation_address();
        if (address.empty()) return request.device;
        const std::string name = context_.name_of ? context_.name_of(address) : std::string();
        return name.empty() || name == address ? address : name + "  " + address;
    }

    void show(Request request)
    {
        const PromptKind kind = settings_bluetooth::prompt_kind(request.method);
        const std::string device = request_device(request);
        // a later authorization of the device the user already allowed in this session passes at once
        if (session_.auto_accept(request.method, device)) {
            answer(request, true, {});
            return;
        }
        if (kind == PromptKind::Unsupported) {
            answer(request, false, {});
            return;
        }
        // BlueZ repeats DisplayPasskey while the keys are typed on the remote: the same code stays as it is
        if (dialog_ && settings_bluetooth::prompt_is_display(kind) && kind_ == kind && current_.device == request.device &&
            current_.value == request.value)
            return;
        if (dialog_) {
            Request previous = std::move(current_);
            close();
            answer(previous, false, {}); /* a newer request replaces it (the legacy page's rule) */
        }
        current_ = std::move(request);
        kind_ = kind;
        open();
    }

    void open()
    {
        lv_obj_t *focused = context_.group ? lv_group_get_focused(context_.group) : nullptr;
        previous_focus_ = focused;

        cp0::ui::DialogSpec spec;
        const bool input = settings_bluetooth::prompt_takes_input(kind_);
        // a PIN / passkey entry is titled with the device (as the Wi-Fi password with the network): on a small screen
        // with the number pad up that title goes into the page header, the field and its error stay in the dialog
        spec.title = input ? device_title(current_) : settings_bluetooth::prompt_title(kind_);
        spec.message_line = input;
        spec.page = &context_.page;
        if (settings_bluetooth::prompt_is_display(kind_)) {
            spec.buttons = {{"OK", cp0::ui::ButtonRole::Primary, [this] { poster_.post([this] { close(); }); }}};
        } else if (input) {
            spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { poster_.post([this] { reject(); }); }},
                            {"OK", cp0::ui::ButtonRole::Primary, [this] { poster_.post([this] { accept(); }); }}};
        } else {
            spec.buttons = {{"No", cp0::ui::ButtonRole::Cancel, [this] { poster_.post([this] { reject(); }); }},
                            {"Yes", cp0::ui::ButtonRole::Primary, [this] { poster_.post([this] { accept(); }); }}};
        }
        dialog_ = std::make_unique<cp0::ui::Dialog>(context_.page.root(), context_.page.layout(), std::move(spec));
        if (!input) dialog_->add_text(device_line(current_));
        switch (kind_) {
        case PromptKind::Confirmation:
            dialog_->add_text("Pairing code: " + (current_.value.empty() ? std::string("(not provided)") : current_.value));
            break;
        case PromptKind::Authorization:
            dialog_->add_text("Allow this device to connect?");
            break;
        case PromptKind::ServiceAuthorization:
            dialog_->add_text("Service: " + (current_.value.empty() ? std::string("(not specified)") : current_.value));
            break;
        case PromptKind::DisplayPasskey:
        case PromptKind::DisplayPinCode:
            dialog_->add_text("Type this code on the device, then press its Enter:");
            dialog_->add_text(current_.value);
            break;
        case PromptKind::PinCode:
        case PromptKind::Passkey: {
            cp0::ui::TextFieldSpec field;
            field.label = kind_ == PromptKind::PinCode ? "Bluetooth PIN code" : "Bluetooth passkey";
            field.type = cp0::ui::TextType::Number; /* the number pad */
            field.max_bytes = settings_bluetooth::prompt_max_input(kind_);
            cp0::ui::TextFieldEvents events;
            events.submit = [this](const std::string &) { poster_.post([this] { accept(); }); };
            events.escape = [this] { poster_.post([this] { reject(); }); };
            events.back = events.escape;
            field_ = std::make_unique<cp0::ui::TextField>(dialog_->body(), dialog_->field_layout(), field,
                                                          std::move(events));
            break;
        }
        case PromptKind::Unsupported:
            break;
        }
        dialog_->relayout();
        if (field_) focus_in(context_.group, field_->focus_object()); /* it edits: the number pad comes */
        else focus_in(context_.group, dialog_->focus_object());      /* Enter = Yes / OK, Esc = No */
    }

    void accept()
    {
        if (!dialog_) return;
        std::string text;
        if (field_) {
            text = field_->text();
            const std::string error = settings_bluetooth::agent_input_error(current_.method, text);
            if (!error.empty()) {
                dialog_->set_message(error, true);
                field_->begin_edit();
                return;
            }
        }
        session_.accepted(current_.method, request_device(current_),
                          context_.operation_pending && context_.operation_pending());
        Request request = std::move(current_);
        close();
        answer(request, true, text);
    }

    void reject()
    {
        if (!dialog_) return;
        Request request = std::move(current_);
        close();
        answer(request, false, {});
    }

    /* The dialog goes; the focus returns where it was (a list, a field of the page's own dialog). */
    void close()
    {
        if (!dialog_) return;
        lv_obj_t *previous = previous_focus_;
        previous_focus_ = nullptr;
        if (previous && lv_obj_is_valid(previous) && context_.group && lv_obj_get_group(previous) == context_.group)
            lv_group_focus_obj(previous);
        field_.reset(); /* it stops editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        kind_ = PromptKind::Unsupported;
        current_ = Request{};
    }

    Context context_;
    settings_bluetooth::AgentSession session_;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    cp0::SignalRegistration<decltype(cp0_signal_bt_agent)> registration_;
    lv_timer_t *timer_ = nullptr;
    Poster poster_;
    Request current_;
    PromptKind kind_ = PromptKind::Unsupported;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::unique_ptr<cp0::ui::TextField> field_;
    lv_obj_t *previous_focus_ = nullptr;
};

/* ------------------------------------------------------------------ Paired Devices / Scan */

class ListPanel : public SettingsNativePanel {
public:
    ListPanel(ListMode mode, const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), title_(context.title), host_back_(context.back), mode_(mode)
    {
        transport_ = std::make_unique<Transport>();
        prompts_ = std::make_unique<AgentPrompts>(AgentPrompts::Context{
            page_, group_, [this] { return model_ && model_->op() != Op::None; },
            [this] { return model_ ? model_->op_address() : std::string(); },
            [this](const std::string &address) {
                const DeviceRecord *device = model_ ? model_->device(address) : nullptr;
                return device ? settings_bluetooth::device_label(*device) : std::string();
            }});

        DeviceListModel::Hooks hooks;
        hooks.request = transport_->function();
        hooks.named_only = [] { return settings_bluetooth_named_only_enabled(); };
        hooks.changed = [this] { changed(); };
        hooks.ended = [this](Op op, bool ok) { ended(op, ok); };
        hooks.agent = &prompts_->session();
        model_ = std::make_unique<DeviceListModel>(mode_, std::move(hooks));

        cp0::ui::RollerEvents events;
        events.activate = [this](int row) { activate(row); };
        events.back = host_back_;
        roller_ = std::make_unique<cp0::ui::Roller>(page_.root(), page_.layout(), std::move(events));
        focus_in(group_, roller_->focus_object());
        rebuild(0);
        show_status();
        retry_timer_ = lv_timer_create(retry_cb, kStatusRetryMs, this);
        if (mode_ == ListMode::Scan) scan_timer_ = lv_timer_create(scan_cb, kScanRefreshMs, this);
        model_->open();
    }

    ~ListPanel() override
    {
        if (retry_timer_) lv_timer_delete(retry_timer_);
        if (scan_timer_) lv_timer_delete(scan_timer_);
        retry_timer_ = scan_timer_ = nullptr;
        poster_.reset();
        model_->leave(true); /* discovery stops, a pairing in flight is cancelled in BlueZ */
        fields_.clear();     /* they stop editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        prompts_.reset(); /* any request still open is rejected */
        transport_.reset(); /* joins the workers: what leave() sent has reached the service */
        model_.reset();
        roller_.reset();
        page_.set_caption("", false);
    }

    bool back() override
    {
        if (prompts_ && prompts_->back()) return true;
        if (dialog_) {
            cancel_dialog();
            return true;
        }
        if (level_ == Level::Actions) {
            set_level(Level::List, list_row_of(action_address_));
            return true;
        }
        // the legacy page refuses Esc while a failed pairing is being cleaned up in BlueZ
        return !model_->leave();
    }

private:
    enum class Level { List, Actions };
    enum class RowType { Scan, Device, Placeholder, Connect, Disconnect, Forget, Rename };
    enum class DialogKind { None, Power, Forget, Rename };

    struct Row {
        RowType type;
        std::string address;
    };

    static std::string key_of(const Row &row)
    {
        return std::to_string(static_cast<int>(row.type)) + ":" + row.address;
    }

    void post(std::function<void()> callback)
    {
        if (poster_) poster_->post(std::move(callback));
    }

    /* Focus obj; while an agent prompt is up it keeps the keys and obj gets them when it closes. */
    void take_focus(lv_obj_t *obj)
    {
        if (prompts_ && prompts_->showing()) {
            if (group_ && obj && lv_obj_get_group(obj) != group_) lv_group_add_obj(group_, obj);
            prompts_->set_return_focus(obj);
            return;
        }
        focus_in(group_, obj);
    }

    // ---------------------------------------------------------------- rows

    int list_row_of(const std::string &address) const
    {
        const int first = mode_ == ListMode::Scan ? 1 : 0;
        const auto &devices = model_->devices();
        for (std::size_t i = 0; i < devices.size(); ++i)
            if (devices[i].address == address) return first + static_cast<int>(i);
        return 0;
    }

    cp0::ui::RowSpec device_spec(const DeviceRecord &device) const
    {
        cp0::ui::RowSpec spec;
        if (model_->op() != Op::None && model_->op_address() == device.address) {
            spec.kind = cp0::ui::RowKind::Busy;
            spec.label = model_->op_message();
            return spec;
        }
        spec.label = settings_bluetooth::device_label(device);
        spec.value = settings_bluetooth::device_state(device);
        return spec;
    }

    void list_rows(std::vector<Row> &rows, std::vector<cp0::ui::RowSpec> &specs) const
    {
        if (mode_ == ListMode::Scan) {
            cp0::ui::RowSpec toggle;
            toggle.kind = cp0::ui::RowKind::Toggle;
            toggle.label = "Scan";
            toggle.on = model_->scanning();
            rows.push_back({RowType::Scan, {}});
            specs.push_back(toggle);
        }
        for (const DeviceRecord &device : model_->devices()) {
            rows.push_back({RowType::Device, device.address});
            specs.push_back(device_spec(device));
        }
        if (!model_->devices().empty()) return;
        cp0::ui::RowSpec placeholder;
        if (model_->power_off()) {
            placeholder.label = "Bluetooth is off";
        } else if (mode_ == ListMode::Scan) {
            if (model_->scanning()) {
                placeholder.kind = cp0::ui::RowKind::Busy;
                placeholder.label = "Scanning...";
            } else {
                placeholder.label = "No devices found";
            }
        } else if (!model_->listed() && model_->error().empty()) {
            placeholder.kind = cp0::ui::RowKind::Busy;
            placeholder.label = "Loading...";
        } else {
            placeholder.label = "No paired devices";
        }
        rows.push_back({RowType::Placeholder, {}});
        specs.push_back(placeholder);
    }

    void action_rows(std::vector<Row> &rows, std::vector<cp0::ui::RowSpec> &specs) const
    {
        const DeviceRecord *device = model_->device(action_address_);
        const bool connected = device ? device->connected : action_connected_;
        const std::pair<RowType, const char *> entries[] = {
            {connected ? RowType::Disconnect : RowType::Connect, connected ? "Disconnect" : "Connect"},
            {RowType::Forget, "Forget"},
            {RowType::Rename, "Rename"},
        };
        for (const auto &entry : entries) {
            cp0::ui::RowSpec spec;
            spec.label = entry.second;
            rows.push_back({entry.first, action_address_});
            specs.push_back(spec);
        }
    }

    /* The level's rows; rows that kept their kind change in place (the highlight and the scroll stay), otherwise the
     * list is rebuilt with the highlight on the same row (selected >= 0: on that row instead). */
    void rebuild(int selected = -1)
    {
        if (!roller_) return;
        // the highlight left the followed device: the user moved on
        for (std::size_t i = 0; !follow_address_.empty() && i < rows_.size(); ++i)
            if (rows_[i].type == RowType::Device && rows_[i].address == follow_address_ &&
                static_cast<int>(i) != roller_->selected())
                follow_address_.clear();
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
                // the device of the last operation keeps the highlight through the fresh list that follows it
                if (level_ == Level::List && !follow_address_.empty())
                    for (std::size_t i = 0; i < rows.size(); ++i)
                        if (rows[i].type == RowType::Device && rows[i].address == follow_address_)
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
        if (level_ == Level::Actions) title.push_back(action_label_);
        page_.set_title(title);
        rebuild(selected);
    }

    void show_status()
    {
        bool error = false;
        const std::string text = model_->status_line(error);
        page_.set_caption(text, error);
        if (dialog_) dialog_->raise();
        if (prompts_) prompts_->raise();
    }

    void changed()
    {
        // the device whose actions show went away (forgotten elsewhere, out of range): back to the list
        if (level_ == Level::Actions && !model_->device(action_address_) && model_->op() == Op::None &&
            model_->listed() && !dialog_)
            set_level(Level::List, 0);
        else
            rebuild();
        show_status();
        if (model_->power_off() && dialog_kind_ != DialogKind::Power) open_power_notice();
    }

    void ended(Op op, bool ok)
    {
        if (prompts_) prompts_->operation_ended(op == Op::Pair, ok);
        if (op != Op::Rename || dialog_kind_ != DialogKind::Rename) return;
        if (ok) {
            close_dialog();
            follow_address_ = action_address_;
            set_level(Level::List, list_row_of(action_address_));
            return;
        }
        rename_failed();
    }

    // ---------------------------------------------------------------- activation

    void activate(int index)
    {
        if (index < 0 || index >= static_cast<int>(rows_.size()) || dialog_ || (prompts_ && prompts_->showing())) return;
        const Row row = rows_[static_cast<std::size_t>(index)];
        switch (row.type) {
        case RowType::Scan:
            model_->set_scanning(!model_->scanning());
            break;
        case RowType::Device: {
            const DeviceRecord *device = model_->device(row.address);
            if (!device || model_->op() != Op::None) break;
            if (mode_ == ListMode::Paired || device->paired || device->connected) {
                action_address_ = device->address;
                action_label_ = settings_bluetooth::device_label(*device);
                action_name_ = device->name;
                action_connected_ = device->connected;
                set_level(Level::Actions, 0);
            } else if (model_->pair(device->address)) { /* its row: "Pairing..."; the agent's prompts: dialogs */
                follow_address_ = device->address;
            }
            break;
        }
        case RowType::Connect:
        case RowType::Disconnect: {
            const bool done = row.type == RowType::Connect ? model_->connect(row.address)
                                                           : model_->disconnect(row.address);
            if (done) {
                follow_address_ = row.address;
                set_level(Level::List, list_row_of(row.address)); /* its row shows the operation */
            }
            break;
        }
        case RowType::Forget:
            open_forget();
            break;
        case RowType::Rename:
            open_rename();
            break;
        case RowType::Placeholder:
            break;
        }
        show_status();
    }

    // ---------------------------------------------------------------- dialogs

    void open_power_notice()
    {
        close_dialog();
        dialog_kind_ = DialogKind::Power;
        // the legacy notice; OK (Enter, Esc) goes back to the Bluetooth section
        dialog_ = notice(page_, "Bluetooth power is off", "Turn on Power before continuing.", [this] {
            post([this] {
                close_dialog();
                host_back_();
            });
        });
        enter_dialog();
    }

    void open_forget()
    {
        close_dialog();
        dialog_kind_ = DialogKind::Forget;
        cp0::ui::DialogSpec spec;
        spec.title = "Forget this device?";
        spec.page = &page_;
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { cancel_dialog(); }); }},
                        {"Forget", cp0::ui::ButtonRole::Primary, [this] { post([this] { confirm_forget(); }); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
        dialog_->add_text(action_label_);
        dialog_->add_text("It must be paired again before it can connect.");
        enter_dialog();
    }

    void confirm_forget()
    {
        const std::string address = action_address_;
        close_dialog();
        if (model_->forget(address)) {
            follow_address_ = address;
            set_level(Level::List, list_row_of(address));
        }
        show_status();
    }

    void open_rename()
    {
        close_dialog();
        dialog_kind_ = DialogKind::Rename;
        cp0::ui::DialogSpec spec;
        spec.title = "Rename";
        spec.message_line = true;
        spec.page = &page_; /* small screens with the keyboard up: the title in the header (cp0_ui_dialog.h) */
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { cancel_dialog(); }); }},
                        {"Save", cp0::ui::ButtonRole::Primary, [this] { post([this] { submit_rename(); }); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
        cp0::ui::TextFieldSpec field;
        field.label = "Name";
        field.text = action_name_;
        field.max_bytes = kMaxNameBytes;
        cp0::ui::TextFieldEvents events;
        events.submit = [this](const std::string &) { post([this] { submit_rename(); }); };
        events.escape = [this] { post([this] { cancel_dialog(); }); };
        events.back = events.escape;
        fields_.push_back(
            std::make_unique<cp0::ui::TextField>(dialog_->body(), dialog_->field_layout(), field, std::move(events)));
        enter_dialog();
    }

    void submit_rename()
    {
        if (!dialog_ || dialog_busy_ || fields_.empty()) return;
        const std::string name = fields_.front()->text();
        const std::string error = settings_bluetooth::alias_error(name);
        if (!error.empty()) {
            dialog_->set_message(error, true);
            return;
        }
        if (!model_->rename(action_address_, name)) {
            dialog_->set_message(model_->result().empty() ? "Busy, try again" : model_->result(), true);
            return;
        }
        dialog_busy_ = true;
        dialog_->set_message("Renaming...", false);
        fields_.front()->end_edit(); /* the keyboard goes while it is sent */
    }

    void rename_failed()
    {
        if (!dialog_ || fields_.empty()) return;
        dialog_busy_ = false;
        dialog_->set_message(model_->result(), true);
        take_focus(fields_.front()->focus_object());
        if (!(prompts_ && prompts_->showing())) fields_.front()->begin_edit(); /* the keyboard comes back */
    }

    void enter_dialog()
    {
        dialog_->relayout();
        unfocus(roller_->focus_object()); /* Up / Down / Tab stay in the dialog */
        if (!fields_.empty()) take_focus(fields_.front()->focus_object()); /* it edits: the keyboard comes */
        else take_focus(dialog_->focus_object());                          /* Enter / Esc: its buttons */
        if (prompts_) prompts_->raise();
    }

    void close_dialog()
    {
        if (!dialog_) return;
        take_focus(roller_->focus_object());
        fields_.clear(); /* they stop editing: input context, intercept and the keyboard restored */
        dialog_.reset();
        dialog_kind_ = DialogKind::None;
        dialog_busy_ = false;
    }

    void cancel_dialog()
    {
        const DialogKind kind = dialog_kind_;
        close_dialog();
        if (kind == DialogKind::Power) host_back_();
        show_status();
    }

    // ---------------------------------------------------------------- timers

    static void retry_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<ListPanel *>(lv_timer_get_user_data(timer));
        if (self && self->model_) self->model_->retry_status();
    }

    static void scan_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<ListPanel *>(lv_timer_get_user_data(timer));
        if (self && self->model_) self->model_->scan_tick();
    }

    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::vector<std::string> title_;
    std::function<void()> host_back_;
    ListMode mode_;

    std::unique_ptr<Poster> poster_ = std::make_unique<Poster>();
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<AgentPrompts> prompts_;
    std::unique_ptr<DeviceListModel> model_;
    std::unique_ptr<cp0::ui::Roller> roller_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::vector<std::unique_ptr<cp0::ui::TextField>> fields_;
    lv_timer_t *retry_timer_ = nullptr;
    lv_timer_t *scan_timer_ = nullptr;

    Level level_ = Level::List;
    std::vector<Row> rows_;
    std::vector<cp0::ui::RowSpec> specs_;
    std::string action_address_; /* Actions: the device */
    std::string follow_address_; /* the last operation's device: it keeps the highlight through the next list */
    std::string action_label_;
    std::string action_name_;
    bool action_connected_ = false;
    DialogKind dialog_kind_ = DialogKind::None;
    bool dialog_busy_ = false;
};

/* ------------------------------------------------------------------ Alias */

class AliasPanel : public SettingsNativePanel {
public:
    AliasPanel(const NodeIter &node, const SettingsNativePanelContext &context)
        : node_(node), page_(context.page), group_(context.group), host_back_(context.back)
    {
        // the tree's entry reads "Alias: <name>": the title says "Alias", the field holds the name
        constexpr const char *kPrefix = "Alias: ";
        std::string initial = node_->label;
        if (initial.rfind(kPrefix, 0) == 0) initial.erase(0, std::strlen(kPrefix));
        std::vector<std::string> title = context.title;
        if (!title.empty()) title.back() = "Alias";
        page_.set_title(title);

        transport_ = std::make_unique<Transport>();
        prompts_ = std::make_unique<AgentPrompts>(
            AgentPrompts::Context{page_, group_, [] { return false; }, [] { return std::string(); }, {}});
        settings_bluetooth::AliasModel::Hooks hooks;
        hooks.request = transport_->function();
        hooks.changed = [this] { changed(); };
        hooks.saved = [this](const std::string &alias) { node_->label = std::string(kPrefix) + alias; };
        hooks.done = [this] { host_back_(); };
        model_ = std::make_unique<settings_bluetooth::AliasModel>(initial, std::move(hooks));

        cp0::ui::TextFieldSpec spec;
        spec.label = "Name";
        spec.text = model_->alias();
        spec.max_bytes = kMaxNameBytes;
        cp0::ui::TextFieldEvents events;
        events.changed = [this](const std::string &text) { model_->edited(text); };
        events.submit = [this](const std::string &) { poster_->post([this] { save(); }); };
        events.back = host_back_;
        field_ = std::make_unique<cp0::ui::TextField>(page_.body(), page_.layout(), spec, std::move(events));
        focus_in(group_, field_->focus_object()); /* it edits: the keyboard comes when no physical one is there */
        retry_timer_ = lv_timer_create(retry_cb, kStatusRetryMs, this);
        model_->open();
    }

    ~AliasPanel() override
    {
        if (retry_timer_) lv_timer_delete(retry_timer_);
        retry_timer_ = nullptr;
        poster_.reset();
        field_.reset(); /* it stops editing */
        dialog_.reset();
        page_.clear_body();
        prompts_.reset();
        transport_.reset();
        model_.reset();
        page_.set_caption("", false);
    }

    bool back() override
    {
        if (prompts_ && prompts_->back()) return true;
        if (dialog_) {
            close_notice();
            return false; /* the notice's OK goes back too */
        }
        return model_->saving(); /* the legacy page does not leave while the name is being set */
    }

private:
    void changed()
    {
        if (model_->take_text() && field_) field_->set_text(model_->alias());
        const bool saving = model_->saving();
        if (saving) page_.set_caption("Saving...", false);
        else if (!model_->error().empty()) page_.set_caption(model_->error(), true);
        else page_.set_caption("", false);
        if (dialog_) dialog_->raise();
        if (prompts_) prompts_->raise();
        if (model_->power_off() && !dialog_ && !notice_shown_) open_notice();
        // a refused name: the previous one is back in the field, editing again
        if (was_saving_ && !saving && !model_->error().empty() && field_ && !dialog_ &&
            !(prompts_ && prompts_->showing())) {
            focus_in(group_, field_->focus_object());
            field_->begin_edit();
        }
        was_saving_ = saving;
    }

    void save()
    {
        if (!field_ || model_->saving()) return;
        if (model_->save(field_->text())) {
            field_->end_edit(); /* the keyboard goes while it is set */
            return;
        }
        if (!model_->error().empty() && !model_->power_off()) field_->begin_edit();
        if (model_->power_off()) open_notice();
    }

    void open_notice()
    {
        notice_shown_ = true;
        field_->end_edit();
        dialog_ = notice(page_, "Bluetooth power is off", "Turn on Power before continuing.", [this] {
            poster_->post([this] {
                close_notice();
                host_back_();
            });
        });
        unfocus(field_->focus_object());
        focus_in(group_, dialog_->focus_object());
        if (prompts_) prompts_->raise();
    }

    void close_notice()
    {
        if (!dialog_) return;
        dialog_.reset();
        if (field_) {
            lv_group_t *group = group_;
            if (group && lv_obj_get_group(field_->focus_object()) != group) lv_group_add_obj(group, field_->focus_object());
        }
    }

    static void retry_cb(lv_timer_t *timer)
    {
        auto *self = static_cast<AliasPanel *>(lv_timer_get_user_data(timer));
        if (self && self->model_) self->model_->retry_status();
    }

    NodeIter node_;
    cp0::ui::Page &page_;
    lv_group_t *group_;
    std::function<void()> host_back_;
    std::unique_ptr<Poster> poster_ = std::make_unique<Poster>();
    std::unique_ptr<Transport> transport_;
    std::unique_ptr<AgentPrompts> prompts_;
    std::unique_ptr<settings_bluetooth::AliasModel> model_;
    std::unique_ptr<cp0::ui::TextField> field_;
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    lv_timer_t *retry_timer_ = nullptr;
    bool notice_shown_ = false;
    bool was_saving_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_bluetooth_panel_create(SettingsNativePage page, const NodeIter &node,
                                                                     const SettingsNativePanelContext &context)
{
    switch (page) {
    case SettingsNativePage::BluetoothPaired:
        return std::make_unique<ListPanel>(ListMode::Paired, context);
    case SettingsNativePage::BluetoothScan:
        return std::make_unique<ListPanel>(ListMode::Scan, context);
    case SettingsNativePage::BluetoothAlias:
        return std::make_unique<AliasPanel>(node, context);
    default:
        break;
    }
    return nullptr;
}
