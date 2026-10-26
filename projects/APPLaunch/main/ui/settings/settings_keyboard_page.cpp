/*
 * SPDX-License-Identifier: MIT
 *
 * Settings entries of the fallback on-screen keyboard (see settings_keyboard_page.hpp).
 */

#include "settings_keyboard_page.hpp"

#include "settings_native_mode.hpp"
#include "settings_strokes_page.hpp"

#include "../launcher_osk.hpp"

#include "cp0_ui_widgets.hpp"

#include <utility>
#include <vector>

// ------------------------------------------------------------------ System > On-screen keyboard

namespace {

/* the row of a value in the list (launcher_osk::choices: Auto, On, Handwriting + keyboard on the PDA profile) */
int row_of(int value)
{
    int values[3];
    const int n = launcher_osk::choices(values);
    for (int i = 0; i < n; i++)
        if (values[i] == value) return i;
    return 0;
}

} // namespace

LvSettingKeyboardChoicePage3::LvSettingKeyboardChoicePage3(lv_obj_t *parent, const NodeIter &parent_node,
                                                           std::function<void()> back_callback)
    : LvSettingValuePage3Base(parent_node, {})
{
    current_ = row_of(launcher_osk::mode()); /* read before initialize(): it selects this row */
    back_callback_ = std::move(back_callback);
    LeaveSelfPage = [this] { request_back(); };
    initialize(parent);
}

LvSettingKeyboardChoicePage3::~LvSettingKeyboardChoicePage3()
{
    LeaveSelfPage = nullptr;
    back_callback_ = nullptr;
}

int LvSettingKeyboardChoicePage3::initial_selection() const
{
    return current_;
}

SettingApiResult LvSettingKeyboardChoicePage3::activate_selected()
{
    int values[3];
    const int n = launcher_osk::choices(values);
    if (selected_index < 0 || selected_index >= n) return SettingApiResult::Failure;
    if (selected_index != current_ && !launcher_osk::set_mode(values[selected_index])) return SettingApiResult::Failure;
    current_ = selected_index;
    request_back();
    return SettingApiResult::Success;
}

void LvSettingKeyboardChoicePage3::request_back()
{
    if (back_requested_) return;
    back_requested_ = true;
    if (back_callback_) back_callback_();
}

std::unique_ptr<DComponens::LvglComponensBase> settings_keyboard_choice_page_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback)
{
    return std::make_unique<LvSettingKeyboardChoicePage3>(parent, page_node, std::move(back_callback));
}

// ------------------------------------------------------------------ System > Keyboard test (development)

std::unique_ptr<DComponens::LvglComponensBase> settings_keyboard_test_factory(lv_obj_t *, const NodeIter &,
                                                                              std::function<void()>)
{
    return nullptr; /* native only: settings_native_panel_create() */
}

namespace {

/* Three fields, one of each type, to try the keyboard: Text (letters page), Number (the number pad), Password (masked,
 * with the show / hide toggle). Nothing is stored. */
class KeyboardTestPanel : public SettingsNativePanel {
public:
    KeyboardTestPanel(cp0::ui::Page &page, lv_group_t *group, std::function<void()> back) : page_(page)
    {
        lv_obj_t *body = page.body();
        cp0::ui::TextFieldEvents events;
        events.back = std::move(back);
        add(body, "Text", cp0::ui::TextType::Text, 128, events);
        add(body, "Number", cp0::ui::TextType::Number, 16, events);
        add(body, "Password", cp0::ui::TextType::Password, 64, events);
        if (!group) return;
        for (const auto &field : fields_) lv_group_add_obj(group, field->focus_object());
        lv_group_focus_obj(fields_.front()->focus_object()); /* the first field edits: the keyboard comes */
    }

    ~KeyboardTestPanel() override
    {
        fields_.clear(); /* each stops editing (context, intercept, hint restored; keyboard released) */
        page_.clear_body();
    }

private:
    void add(lv_obj_t *body, const char *label, cp0::ui::TextType type, std::size_t max_bytes,
             const cp0::ui::TextFieldEvents &events)
    {
        cp0::ui::TextFieldSpec spec;
        spec.label = label;
        spec.type = type;
        spec.max_bytes = max_bytes;
        fields_.push_back(std::make_unique<cp0::ui::TextField>(body, page_.layout(), spec, events));
    }

    cp0::ui::Page &page_;
    std::vector<std::unique_ptr<cp0::ui::TextField>> fields_;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_keyboard_test_panel_create(cp0::ui::Page &page, lv_group_t *group,
                                                                         std::function<void()> back)
{
    return std::make_unique<KeyboardTestPanel>(page, group, std::move(back));
}

namespace settings_keyboard {

void append_entries(Tree &tree, const NodeIter &system)
{
    NodeIter keyboard = tree.append_child(system, SettingEntry{"On-screen keyboard", settings_keyboard_choice_page_factory});
    /* the rows in launcher_osk::choices() order (report 042: Off is gone, a stored Off reads as Auto) */
    int values[3];
    const int n = launcher_osk::choices(values);
    for (int i = 0; i < n; i++) {
        const char *label = values[i] == launcher_osk::kOn     ? "On"
                            : values[i] == launcher_osk::kHand ? "Handwriting + keyboard"
                                                               : "Auto";
        tree.append_child(keyboard, SettingEntry{label});
    }
    /* the PDA writing zone's training (report 042), next to the keyboard's entries: the rows counted from the end of
     * the section (Shutdown, Reboot, Keyboard test) stay where they were */
    settings_strokes::append_entry(tree, system);
    if (settings_ui::dev_pages())
        tree.append_child(system, SettingEntry{"Keyboard test", settings_keyboard_test_factory, PageType::Normal});
}

} // namespace settings_keyboard
