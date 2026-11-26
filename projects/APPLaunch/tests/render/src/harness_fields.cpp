/* SPDX-License-Identifier: MIT
 *
 * Render harness: the shared TextField with its selection and clipboard (docs/dev/clipboard.md) and the launcher's
 * clipboard service behind it.
 *
 *   fielddemo                a page with six TextFields: Name (text, a few accents), Address (long: scrolls), Port
 *                            (number), Secret (password), Short (8 bytes at most), Raw (the app keeps the Ctrl keys); the first is in the input group and
 *                            focused (a scene taps or moves to another)
 *   clipboard server on|off|old|slow
 *                            the real launcher.sock protocol (app_request.cpp, app_request_server.cpp, the framing of
 *                            `clipboard set-len`) on a short socket path (APPLAUNCH_SOCKET), in this process, with the
 *                            real client library on the other end: on = a launcher with the clipboard; off = no socket (an
 *                            app with no launcher); old = a launcher that answers `err unknown` to everything (before the
 *                            clipboard verb); slow = one that answers after 400 ms (the toolkit's 100 ms timeout gives up)
 *   clipboard set TEXT       puts TEXT on it through the client (`\n` = a newline, `\t` a tab, `\\` a backslash)
 *   clipboard expect TEXT | empty    fails the scene unless the clipboard holds exactly TEXT (or nothing)
 *   clipboard clear          empties it through the client
 *   clipboard logged TEXT    fails the scene when TEXT occurs in any line the launcher side logged
 * Nothing here is linked into the launcher.
 */
#include "harness.h"

#include "app_request.hpp"
#include "app_request_server.hpp"
#include "cp0_app_client.h"
#include "cp0_ui_metrics.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"
#include "cp0_ui_widgets.hpp"
#include "lvgl/lvgl.h"
#include "native_ui.hpp"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace harness {

namespace {

/* ---------------------------------------------------------------- the demo page */

struct FieldDemo {
    lv_obj_t *screen = nullptr;
    std::unique_ptr<cp0::ui::Page> page;
    std::vector<std::unique_ptr<cp0::ui::TextField>> fields;
    std::vector<std::string> labels;
    std::vector<std::unique_ptr<cp0::ui::CopyButton>> copies; /* copydemo */
    std::vector<std::string> values;                           /* what each Copy button copies (copydemo) */
    lv_group_t *group = nullptr;
    int row_h = 0;
};

FieldDemo g_demo;

void demo_destroy(FieldDemo &d)
{
    d.fields.clear();
    d.copies.clear();
    d.page.reset();
    if (d.group) lv_group_delete(d.group);
    d.group = nullptr;
    if (d.screen) lv_obj_delete(d.screen);
    d.screen = nullptr;
}

cp0::ui::TextField *find_field(const std::string &label)
{
    for (size_t i = 0; i < g_demo.labels.size(); ++i)
        if (g_demo.labels[i] == label) return g_demo.fields[i].get();
    return nullptr;
}

/* the field's text area (the first child of its focus object) and the label the text is drawn on */
lv_obj_t *field_label(cp0::ui::TextField *field)
{
    lv_obj_t *area = field && field->focus_object() ? lv_obj_get_child(field->focus_object(), 0) : nullptr;
    return area ? lv_textarea_get_label(area) : nullptr;
}

/* ---------------------------------------------------------------- the clipboard service */

struct Quiet : app_request::Backend {
    std::string hello(const app_request::Peer &) override { return "ok 1"; }
    std::string caps() override { return "ok"; }
    std::string beep(const std::string &) override { return "ok"; }
    std::string toast(const std::string &) override { return "ok queued"; }
    std::string volume(const std::string &, int) override { return "ok"; }
    std::string brightness(const std::string &, int) override { return "ok"; }
    std::string dim(bool, const app_request::Peer &) override { return "ok"; }
    std::string screen(bool, const app_request::Peer &) override { return "ok"; }
};

struct Service {
    Quiet backend;
    app_request::RateLimiter limiter;
    long long fake_now = 1000000;
    std::mutex log_mutex;
    std::vector<std::string> log;
    std::string path;
    std::string mode = "off";
};

Service &service()
{
    static Service *s = new Service(); /* never destroyed: the server thread may outlive main's statics */
    return *s;
}

void service_log(const std::string &line)
{
    Service &s = service();
    std::lock_guard<std::mutex> lock(s.log_mutex);
    s.log.push_back(line);
}

void service_stop()
{
    app_request::stop_server();
    unsetenv("APPLAUNCH_SOCKET");
}

bool service_start(const std::string &mode, std::string *err)
{
    Service &s = service();
    service_stop();
    s.mode = mode;
    if (mode == "off") return true;
    /* a short path: the harness's build tree path would pass sun_path's 108 bytes */
    s.path = "/tmp/cdz-harness-" + std::to_string(::getpid()) + ".sock";
    setenv("APPLAUNCH_SOCKET", s.path.c_str(), 1);
    app_request::ServerConfig config;
    config.path = s.path;
    config.log = service_log;
    if (mode == "old") {
        config.handler = [](const std::string &, const app_request::Peer &) { return std::string("err unknown"); };
    } else {
        const bool slow = mode == "slow";
        config.handler = [slow](const std::string &line, const app_request::Peer &peer) {
            Service &sv = service();
            if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(400));
            sv.fake_now += 1000; /* far apart: the rate limits have their own unit test */
            return app_request::handle_line(line, peer, sv.backend, sv.limiter, sv.fake_now, service_log);
        };
        config.frame = [](const std::string &line, size_t &bytes) { return app_request::payload_length(line, bytes); };
        config.payload_handler = [slow](const std::string &line, const std::string &payload,
                                        const app_request::Peer &peer) {
            Service &sv = service();
            if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(400));
            sv.fake_now += 1000;
            return app_request::handle_payload(line, payload, peer, sv.backend, sv.limiter, sv.fake_now, service_log);
        };
    }
    IgnoreNewThreads ignore; /* the server thread lives as long as the scene wants it */
    if (!app_request::start_server(std::move(config))) {
        if (err) *err = "the clipboard server could not start on " + s.path;
        return false;
    }
    return true;
}

std::string unescape(const std::string &text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char c = text[++i];
            out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
        } else {
            out += text[i];
        }
    }
    return out;
}

} // namespace

void field_demo_open()
{
    FieldDemo old = std::move(g_demo);
    g_demo = FieldDemo{};
    FieldDemo &d = g_demo;
    native_ui::begin_page(true, false);
    d.screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(d.screen);
    lv_obj_set_style_bg_color(d.screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d.screen, LV_OPA_COVER, 0);
    cp0_ui_roller_page_t layout{};
    cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &layout);
    d.page = std::make_unique<cp0::ui::Page>(
        d.screen, layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); }, [] {});
    d.page->set_title({"Field demo"});
    d.row_h = layout.row_h;

    struct Spec {
        const char *label;
        cp0::ui::TextType type;
        const char *text;
        size_t max;
    };
    const Spec specs[] = {
        {"Name", cp0::ui::TextType::Text, "caf\xc3\xa9 \xe2\x82\xac" "5 and more", 40},
        {"Address", cp0::ui::TextType::Text,
         "https://example.test/a/very/long/path/that/does/not/fit/in/the/field/at/all?x=1", 128},
        {"Port", cp0::ui::TextType::Number, "8080", 5},
        {"Secret", cp0::ui::TextType::Password, "hunter2", 32},
        {"Short", cp0::ui::TextType::Text, "abc", 8},
        {"Raw", cp0::ui::TextType::Text, "line", 16}, /* clipboard_keys off: a terminal's line field */
    };
    lv_obj_t *body = d.page->body();
    for (const Spec &spec : specs) {
        cp0::ui::TextFieldSpec fs;
        fs.label = spec.label;
        fs.type = spec.type;
        fs.text = spec.text;
        fs.max_bytes = spec.max;
        fs.clipboard_keys = std::string(spec.label) != "Raw";
        fs.clipboard_strip = std::string(spec.label) != "Raw"; /* an app that keeps the row: no strip either */
        d.labels.push_back(spec.label);
        d.fields.push_back(std::make_unique<cp0::ui::TextField>(body, layout, fs, cp0::ui::TextFieldEvents{}));
    }
    d.group = lv_group_create();
    lv_group_set_wrap(d.group, false);
    for (auto &field : d.fields) lv_group_add_obj(d.group, field->focus_object());
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(indev, d.group);
    lv_screen_load(d.screen);
    cp0::ui::StatusStrip::add(d.page->header(), native_ui::fill_status);
    lv_group_focus_obj(d.fields[0]->focus_object());
    demo_destroy(old);
}

bool field_state(const std::string &label, FieldState *out, std::string *err)
{
    cp0::ui::TextField *field = find_field(label);
    lv_obj_t *text_label = field_label(field);
    if (!field || !text_label) {
        if (err) *err = "no field " + label + " (fielddemo first)";
        return false;
    }
    lv_obj_update_layout(text_label);
    out->text = field->text();
    out->editing = field->editing();
    out->selecting = field->has_selection();
    lv_obj_t *area = lv_obj_get_child(field->focus_object(), 0);
    out->caret = static_cast<int>(lv_textarea_get_cursor_pos(area));
    const uint32_t start = lv_label_get_text_selection_start(text_label);
    const uint32_t end = lv_label_get_text_selection_end(text_label);
    out->sel_start = start == LV_LABEL_TEXT_SELECTION_OFF ? -1 : static_cast<int>(start);
    out->sel_end = end == LV_LABEL_TEXT_SELECTION_OFF ? -1 : static_cast<int>(end);
    lv_area_t box;
    lv_obj_get_coords(field->focus_object(), &box);
    out->box_x1 = box.x1;
    out->box_y1 = box.y1;
    out->box_x2 = box.x2;
    out->box_y2 = box.y2;
    lv_area_t coords;
    lv_obj_get_coords(text_label, &coords);
    out->label_x1 = coords.x1;
    out->label_y1 = coords.y1;
    out->label_y2 = coords.y2;
    /* the selected span in screen x, from the label's own letter positions */
    out->span_x1 = out->span_x2 = 0;
    if (out->sel_start >= 0 && out->sel_end >= 0) {
        lv_point_t a, b;
        lv_label_get_letter_pos(text_label, static_cast<uint32_t>(std::min(out->sel_start, out->sel_end)), &a);
        lv_label_get_letter_pos(text_label, static_cast<uint32_t>(std::max(out->sel_start, out->sel_end)), &b);
        out->span_x1 = coords.x1 + a.x;
        out->span_x2 = coords.x1 + b.x;
    }
    return true;
}

/* a strip button's object, state 0 / 1 / 2 */
static int button_state(lv_obj_t *button)
{
    if (!button || lv_obj_has_flag(button, LV_OBJ_FLAG_HIDDEN) || lv_obj_has_flag(lv_obj_get_parent(button), LV_OBJ_FLAG_HIDDEN))
        return 0;
    return lv_obj_has_state(button, LV_STATE_DISABLED) ? 1 : 2;
}

bool strip_state(const std::string &label, StripState *out, std::string *err)
{
    cp0::ui::TextField *field = find_field(label);
    if (!field) {
        if (err) *err = "no field " + label + " (fielddemo first)";
        return false;
    }
    lv_obj_update_layout(field->row());
    using Clip = cp0::ui::TextField::Clip;
    out->present = field->clip_strip() != nullptr;
    out->editing = field->editing();
    out->row_h = g_demo.row_h;
    lv_area_t box;
    lv_obj_get_coords(field->focus_object(), &box);
    out->box_y2 = box.y2;
    out->visible_bottom = static_cast<int>(lv_display_get_vertical_resolution(lv_obj_get_display(field->row()))) -
                          cp0::ui::osk::bottom_inset();
    if (!out->present) return true;
    out->shown = !lv_obj_has_flag(field->clip_strip(), LV_OBJ_FLAG_HIDDEN);
    out->paste = button_state(field->clip_button(Clip::Paste));
    out->copy = button_state(field->clip_button(Clip::Copy));
    out->clear = button_state(field->clip_button(Clip::Clear));
    if (lv_obj_t *copy = field->clip_button(Clip::Copy)) out->copy_text = lv_label_get_text(lv_obj_get_child(copy, 0));
    bool first = true;
    for (Clip which : {Clip::Paste, Clip::Copy, Clip::Clear}) {
        lv_obj_t *button = field->clip_button(which);
        if (button_state(button) == 0) continue;
        lv_area_t a;
        lv_obj_get_coords(button, &a);
        out->btn_h = a.y2 - a.y1 + 1;
        out->x1 = first ? a.x1 : std::min(out->x1, static_cast<int>(a.x1));
        out->y1 = first ? a.y1 : std::min(out->y1, static_cast<int>(a.y1));
        out->x2 = first ? a.x2 : std::max(out->x2, static_cast<int>(a.x2));
        out->y2 = first ? a.y2 : std::max(out->y2, static_cast<int>(a.y2));
        first = false;
    }
    return true;
}

static lv_obj_t *copy_button_obj(int index)
{
    return index >= 0 && static_cast<size_t>(index) < g_demo.copies.size() ? g_demo.copies[index]->obj() : nullptr;
}

bool strip_point(const std::string &name, const std::string &which, int *x, int *y, std::string *err)
{
    lv_obj_t *button = nullptr;
    if (!name.empty() && name[0] == '@') {
        button = copy_button_obj(std::atoi(name.c_str() + 1));
    } else if (cp0::ui::TextField *field = find_field(name)) {
        using Clip = cp0::ui::TextField::Clip;
        button = field->clip_button(which == "Paste" ? Clip::Paste : which == "Copy" ? Clip::Copy : Clip::Clear);
        if (field->clip_strip()) lv_obj_update_layout(field->row());
    }
    if (!button || button_state(button) == 0) {
        if (err) *err = "no shown " + which + " button for " + name;
        return false;
    }
    lv_area_t a;
    lv_obj_get_coords(button, &a);
    *x = (a.x1 + a.x2) / 2;
    *y = (a.y1 + a.y2) / 2;
    return true;
}

bool copy_state(int index, CopyState *out, std::string *err)
{
    lv_obj_t *button = copy_button_obj(index);
    if (!button) {
        if (err) *err = "no Copy button @" + std::to_string(index) + " (copydemo first)";
        return false;
    }
    lv_obj_update_layout(button);
    out->copied = g_demo.copies[index]->copied();
    out->enabled = !lv_obj_has_state(button, LV_STATE_DISABLED);
    lv_area_t a;
    lv_obj_get_coords(button, &a);
    out->x1 = a.x1; out->y1 = a.y1; out->x2 = a.x2; out->y2 = a.y2;
    out->text = lv_label_get_text(lv_obj_get_child(button, 0));
    return true;
}

/* A page with read-only values and their Copy buttons: an address with the round button at the end of its row, a
 * block value with the pill, and a value that is empty (greyed). */
void copy_demo_open()
{
    FieldDemo old = std::move(g_demo);
    g_demo = FieldDemo{};
    FieldDemo &d = g_demo;
    native_ui::begin_page(true, false);
    d.screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(d.screen);
    lv_obj_set_style_bg_color(d.screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d.screen, LV_OPA_COVER, 0);
    cp0_ui_roller_page_t layout{};
    cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &layout);
    d.page = std::make_unique<cp0::ui::Page>(
        d.screen, layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); }, [] {});
    d.page->set_title({"Copy demo"});
    d.row_h = layout.row_h;
    lv_obj_t *body = d.page->body();
    d.values = {"329a4f1c8be277d05c09a1e3b6f4c109", "https://example.test/a/long/value", ""};
    auto text_line = [&](const char *text) {
        lv_obj_t *label = lv_label_create(body);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, cp0_ui_font_px(layout.value_px), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_width(label, layout.roller_w);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    };
    /* the round button at the end of a row */
    lv_obj_t *row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, layout.roller_w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *value = lv_label_create(row);
    lv_label_set_text(value, "329a...c109");
    lv_obj_set_style_text_font(value, cp0_ui_font_px(layout.value_px), 0);
    lv_obj_set_style_text_color(value, lv_color_hex(0x2ECC71), 0);
    d.copies.push_back(std::make_unique<cp0::ui::CopyButton>(row, layout, [] { return g_demo.values[0]; }, false));
    /* the pill under a block value */
    text_line("329a 4f1c 8be2 77d0 5c09 a1e3 b6f4 c109");
    d.copies.push_back(std::make_unique<cp0::ui::CopyButton>(body, layout, [] { return g_demo.values[1]; }, true));
    /* nothing to copy */
    d.copies.push_back(std::make_unique<cp0::ui::CopyButton>(body, layout, [] { return g_demo.values[2]; }, true));
    lv_screen_load(d.screen);
    cp0::ui::StatusStrip::add(d.page->header(), native_ui::fill_status);
    demo_destroy(old);
}

bool field_char_point(const std::string &label, int index, int *x, int *y, std::string *err)
{
    cp0::ui::TextField *field = find_field(label);
    lv_obj_t *text_label = field_label(field);
    if (!field || !text_label) {
        if (err) *err = "no field " + label + " (fielddemo first)";
        return false;
    }
    lv_obj_update_layout(text_label);
    lv_area_t coords;
    lv_obj_get_coords(text_label, &coords);
    lv_point_t pos;
    lv_label_get_letter_pos(text_label, static_cast<uint32_t>(index), &pos);
    *x = coords.x1 + pos.x + 1; /* just right of the character's left edge: the caret goes before it */
    *y = (coords.y1 + coords.y2) / 2;
    return true;
}

void clipboard_reset()
{
    service_stop();
}

bool clipboard_command(const std::vector<std::string> &w, std::string *err)
{
    auto bad = [&](const std::string &why) {
        if (err) *err = why;
        return false;
    };
    if (w.size() == 3 && w[1] == "server") {
        if (w[2] != "on" && w[2] != "off" && w[2] != "old" && w[2] != "slow")
            return bad("usage: clipboard server on|off|old|slow");
        return service_start(w[2], err);
    }
    char text[CP0_APP_CLIPBOARD_MAX + 1];
    size_t len = 0;
    if (w.size() == 3 && w[1] == "set") {
        const std::string value = unescape(w[2]);
        const int r = cp0_app_clipboard_set(value.data(), value.size(), 2000);
        if (r != CP0_APP_CLIPBOARD_OK) return bad("clipboard set was not accepted (" + std::to_string(r) + ")");
        return true;
    }
    if (w.size() == 2 && w[1] == "clear") {
        const int r = cp0_app_clipboard_clear(2000);
        if (r != CP0_APP_CLIPBOARD_OK) return bad("clipboard clear was not accepted (" + std::to_string(r) + ")");
        return true;
    }
    if (w.size() == 3 && w[1] == "expect") {
        const int r = cp0_app_clipboard_get(text, sizeof(text), &len, 2000);
        if (r != CP0_APP_CLIPBOARD_OK) return bad("clipboard expect: cannot read it (" + std::to_string(r) + ")");
        const std::string have(text, len);
        const std::string want = w[2] == "empty" ? std::string() : unescape(w[2]);
        if (have != want) return bad("clipboard expect: it holds \"" + have + "\", not \"" + want + "\"");
        std::printf("clipboard expect ok: %zu bytes\n", have.size());
        return true;
    }
    if (w.size() == 3 && w[1] == "logged") {
        Service &s = service();
        std::lock_guard<std::mutex> lock(s.log_mutex);
        const std::string needle = unescape(w[2]);
        for (const std::string &line : s.log)
            if (line.find(needle) != std::string::npos) return bad("clipboard logged: \"" + line + "\" holds the text");
        std::printf("clipboard logged ok: %zu log lines, none holds the text\n", s.log.size());
        return true;
    }
    return bad("usage: clipboard server on|off|old|slow | set TEXT | expect TEXT|empty | clear | logged TEXT");
}

} // namespace harness
