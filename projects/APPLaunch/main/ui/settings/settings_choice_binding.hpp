/*
 * SPDX-License-Identifier: MIT
 *
 * ChoiceBinding: the logic of a Settings value page (a list of options, one of them current), whoever draws it
 * (task 014 P2a, report 022 section 3.5). The legacy 320x150 value roller (LvSettingValuePage3Base and its
 * subclasses: Brightness incl. the gpio On / Off backlight, DarkTime, Touch, Date & Time fields, Reboot / Shutdown
 * confirmations) implements it, so the native Settings host drives the very same objects and no logic is copied:
 *
 *   - the host creates the page through the tree node's own page_factory, with a "binding host" as the parent (an
 *     LVGL object tagged by settings_binding_host_create()): the page then builds no 320x150 view, only its logic
 *     (loads the current value, keeps its async tasks, rollbacks, countdowns);
 *   - choice_activate(index) applies an option exactly as Enter on that row of the legacy page does;
 *   - the page reports its selection moves and its status line through SettingsChoiceObserver;
 *   - when the page is done (applied, or nothing to change) it calls the back callback the factory got, as it
 *     always did. A page that called it is finished: the host destroys it and creates a fresh one to show the new
 *     value (the legacy page objects are single-use too).
 *
 * LVGL thread only.
 */
#pragma once

#include "lvgl/lvgl.h"
#include "settings_tree_types.hpp"

#include <string>

class SettingsChoiceObserver {
public:
    virtual ~SettingsChoiceObserver() = default;
    /* the page moved its selection by itself: the current value was loaded, or a failed write rolled back */
    virtual void choice_selected(int index) = 0;
    /* the page's status line ("Loading dark time", "Brightness save failed", "On again in 7 s", ""...) */
    virtual void choice_status(const std::string &text, bool error) = 0;
};

class SettingsChoiceBinding {
public:
    virtual ~SettingsChoiceBinding() = default;
    virtual int choice_count() const = 0;
    /* the option the page shows as current (initial_selection() until its value is loaded) */
    virtual int choice_selection() const = 0;
    /* false while the current value is still being read: choice_selection() is only a default then */
    virtual bool choice_ready() const { return true; }
    /* false for a confirmation (Yes / No): there is no current value to show */
    virtual bool choice_shows_value() const { return true; }
    /* apply option index (Enter on that row): Success = done (the back callback ran), Pending = in progress (the
     * outcome arrives as choice_selected / choice_status and, on success, the back callback), Failure, NotHandled */
    virtual SettingApiResult choice_activate(int index) = 0;
    virtual void choice_set_observer(SettingsChoiceObserver *observer) = 0;
};

/* The parent that makes a value page logic-only (see above): hidden, never drawn. */
inline void *settings_binding_host_tag()
{
    static char tag;
    return &tag;
}

inline lv_obj_t *settings_binding_host_create(lv_obj_t *parent)
{
    lv_obj_t *host = lv_obj_create(parent);
    lv_obj_remove_style_all(host);
    lv_obj_set_size(host, 0, 0);
    lv_obj_add_flag(host, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(host, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(host, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(host, settings_binding_host_tag());
    return host;
}

inline bool settings_is_binding_host(lv_obj_t *parent)
{
    return parent && lv_obj_get_user_data(parent) == settings_binding_host_tag();
}
