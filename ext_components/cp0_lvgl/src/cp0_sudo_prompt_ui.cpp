/*
 * SPDX-License-Identifier: MIT
 *
 * The responsive sudo password prompt (see cp0_sudo_prompt_ui.hpp).
 */

#include "cp0_sudo_prompt_ui.hpp"

#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"

#include <cstdint>
#include <exception>
#include <utility>

namespace cp0_sudo {
namespace {

constexpr std::size_t kMaxPasswordBytes = 128; /* cp0_sudo_async's limit */
constexpr unsigned kSubmit = 1;
constexpr unsigned kCancel = 2;

PromptUi *g_active = nullptr; /* the prompt hops are delivered to: one at a time */
unsigned g_generation = 0;    /* a hop of a deleted prompt finds another generation and does nothing */

} // namespace

PromptUi::~PromptUi()
{
    if (g_active == this) g_active = nullptr;
    field_.reset(); /* stops editing: input context, intercept and the keyboard restored; zeroes its text */
    dialog_.reset();
}

bool PromptUi::create(lv_obj_t *layer, PromptUiEvents events)
{
    if (!layer || dialog_) return false;
    cp0_ui_roller_page_t layout{};
    if (cp0_ui_roller_page(cp0_ui_metrics_get(), 0, &layout) != 0) return false;
    /* no page header under the top layer: the dialog may use the whole screen, and its dimmed backdrop covers (and
     * swallows the taps meant for) the header of the page below, Esc button included */
    layout.header_h = 0;
    events_ = std::move(events);
    generation_ = ++g_generation;
    g_active = this;
    try {
        cp0::ui::DialogSpec spec;
        spec.title = "Sudo Password";
        spec.message_line = true;
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post(kCancel); }},
                        {"OK", cp0::ui::ButtonRole::Primary, [this] { post(kSubmit); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(layer, layout, std::move(spec));
        cp0::ui::TextFieldSpec field;
        field.label = "Password";
        field.type = cp0::ui::TextType::Password;
        field.max_bytes = kMaxPasswordBytes;
        cp0::ui::TextFieldEvents field_events;
        field_events.submit = [this](const std::string &) { post(kSubmit); }; /* Enter */
        field_events.escape = [this] { post(kCancel); };                      /* Esc while editing */
        field_events.back = field_events.escape;                              /* Esc while not editing */
        field_ = std::make_unique<cp0::ui::TextField>(dialog_->body(), dialog_->field_layout(), std::move(field),
                                                      std::move(field_events));
        dialog_->relayout();
    } catch (const std::exception &) {
        field_.reset();
        dialog_.reset();
        g_active = nullptr;
        return false;
    }
    return true;
}

lv_obj_t *PromptUi::focus_object() const
{
    return field_ ? field_->focus_object() : nullptr;
}

void PromptUi::begin_edit()
{
    if (field_ && !busy_) field_->begin_edit();
}

void PromptUi::show_busy(const std::string &text)
{
    if (!field_ || !dialog_) return;
    busy_ = true;
    field_->end_edit(); /* the keyboard goes while it authenticates */
    field_->wipe();
    dialog_->set_message(text, false);
    dialog_->set_button_enabled(1, false); /* OK; Cancel stays: Esc cancels while authenticating, so does it */
}

void PromptUi::show_error(const std::string &text)
{
    if (!field_ || !dialog_) return;
    busy_ = false;
    field_->wipe();
    dialog_->set_button_enabled(1, true);
    dialog_->set_message(text, true); /* kept in view when the keyboard comes back (Dialog::relayout) */
    field_->begin_edit();
}

void PromptUi::post(unsigned kind)
{
    const auto user = reinterpret_cast<void *>(static_cast<std::uintptr_t>((generation_ << 2) | kind));
    if (lv_async_call(hop_cb, user) != LV_RESULT_OK) run(kind); /* no queue: act here (the owner copes) */
}

void PromptUi::hop_cb(void *user)
{
    const auto value = reinterpret_cast<std::uintptr_t>(user);
    PromptUi *self = g_active;
    if (!self || (value >> 2) != self->generation_) return;
    self->run(static_cast<unsigned>(value & 3));
}

void PromptUi::run(unsigned kind)
{
    if (!field_) return;
    if (kind == kSubmit) {
        if (busy_) return; /* OK or Enter again while authenticating */
        if (events_.submit) {
            const auto submit = events_.submit; /* the owner may delete this prompt in it */
            submit(field_->text());
        }
    } else if (kind == kCancel && events_.cancel) {
        const auto cancel = events_.cancel;
        cancel();
    }
}

} // namespace cp0_sudo
