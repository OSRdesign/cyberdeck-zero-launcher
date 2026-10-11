/*
 * SPDX-License-Identifier: MIT
 *
 * Shared widgets of the responsive shell (task 014 P2a, report 022 section 3.4): the page scaffold and the roller
 * list with its row kinds. Pages built from them carry no geometry: every size comes from the layout service
 * (cp0_ui_metrics.h) through the roller page layout (cp0_ui_roller.h), and every widget handles touch and the
 * keyboard itself, so a page made only of these widgets works with a finger and with a physical keyboard.
 *
 *   Page    header (the home grid's title, a status line, an on-screen Esc button, room for the shared status
 *           strip) and the area under it. The Esc button calls the page's back action directly: it never injects a
 *           key, so it can never leave a stuck Esc behind (report 022 section 2.2).
 *   Roller  today's centre-highlight roller, full height, width capped and centred. Rows:
 *             Nav     label, current value right-aligned in green (opens something)
 *             Choice  label, blue check mark on the current value
 *             Toggle  label, switch
 *             Slider  label, slider, value text (Left / Right or a drag change it)
 *             Info    (task 014 P3a) key left, value right (cp0_ui_info.h: the key capped, the value cut with dots or,
 *                     on Compact, wrapped onto a second line)
 *             Network (task 014 P3b) a Wi-Fi network: name left; at the right the signal bars (four, the status
 *                     bar's look), a small lock when secured, and a blue check (connected: the name in bold too)
 *                     or the value text ("Saved")
 *             Busy    (task 014 P3b) label and a spinner at the right ("Connecting...")
 *           Touch: a drag scrolls and snaps to a row; a tap on a row moves the highlight there; a tap on the
 *           highlighted row activates it. Keyboard (LV_EVENT_KEY on the page's input group): Up / Down move, Enter
 *           activates, Esc goes back, Left / Right change a slider (elsewhere Right activates and Left goes back, as
 *           in today's Settings).
 *
 *   TextField          (task 014 P2b) a label and a one-line field: Text, Number or Password (masked, with a show /
 *                      hide toggle inside the field). It owns the UTF-8 text and the cursor. Focus (a tap, Up / Down /
 *                      Tab from the next field) starts editing: it sets KBD_INPUT_CONTEXT_TEXT, the LVGL keypad
 *                      intercept and the text hint, reads LV_EVENT_KEYBOARD on its screen and asks for the on-screen
 *                      keyboard; Esc, Enter (default), focus leaving or the field's deletion stop editing and restore all
 *                      three. Esc then Esc again goes back; a tap or Enter on the field edits again.
 *   OnScreenKeyboard   (task 014 P2b) the fallback keyboard (cp0_ui_osk.h: layout, pages, modifiers, key events) drawn
 *                      at the bottom of the display on its top layer. Keys are injected as press + release key_items
 *                      with text (cp0_keyboard_inject_key), so fields, the sudo prompt and old pages read them as a
 *                      physical keyboard's. Only the osk service below creates it: it is never user-summoned.
 *   osk                the keyboard service: shows the keyboard for the field that asked while the host's policy allows
 *                      it (no physical keyboard, setting Auto, no lock screen, no desktop), hides it on release, page
 *                      change or when the policy turns false (a physical keyboard appeared: owed key releases sent,
 *                      latched modifiers dropped), and publishes its height as the bottom inset. Page::body() shrinks
 *                      by that inset and keeps the editing field in view; keep_above_keyboard() keeps a dialog centred
 *                      in the room above it.
 *
 *   InfoTable          (task 014 P3a) a Roller of Info rows for the read-only pages: no row action, the highlight still
 *                      moves (Up / Down, drag, tap) so a long table scrolls; Esc / Left go back.
 *   TextBlock          (task 014 P3a) a long text (Licenses, guides) in a scrolling column, roller wide: drag, Up / Down
 *                      (a row token per press), Page Up / Down, Home / End; Esc / Left go back.
 *   Dialog             (task 014 P3b) a title, a body (text, TextFields) and 1-3 buttons: the primary one is Enter, the
 *                      cancel one Esc (also the keyboard's Enter / Esc while a field edits). Width DIALOG_MAX_W,
 *                      centred in the room between the page header and the on-screen keyboard and following the
 *                      keyboard; the body scrolls; when the keyboard leaves no room the buttons go, then the title
 *                      moves into the page header's title slot (cp0_ui_dialog.h): the header itself, its Esc button
 *                      and the status strip are never covered. A backdrop under it takes the taps meant for the page.
 *                      A notice (one button, task 014 P3c): Enter and Esc both press it.
 *
 * LVGL thread only. The objects are children of the parent given to the constructor; the destructor deletes them
 * (unless the parent went first).
 */
#pragma once

#include "cp0_ui_dialog.h"
#include "cp0_ui_info.h"
#include "cp0_ui_osk.h"
#include "cp0_ui_roller.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cp0 {
namespace ui {

enum class RowKind { Nav, Choice, Toggle, Slider, Info, Network, Busy };

struct RowSpec {
    RowKind kind = RowKind::Nav;
    std::string label;  /* Info: the key */
    std::string value;  /* Nav: current value (green, right); Slider: value text; Info: the value; Network: "Saved" */
    bool on = false;    /* Choice: carries the check mark; Toggle: switch state; Network: connected (check, bold) */
    int slider_min = 0; /* Slider */
    int slider_max = 100;
    int slider_value = 0;
    int slider_step = 1; /* one Left / Right press */
    int bars = 0;        /* Network: signal bars lit, 0..4 */
    bool secured = false; /* Network: the lock */
    int value_rgb = -1;   /* Nav: the value's colour (0xRRGGBB, task 014 P3d: a state such as "failed"); -1 green */
};

struct RollerEvents {
    std::function<void(int row)> activate;          /* Enter, Right off a slider, tap on the highlighted row */
    std::function<void(int row, int value)> slide;  /* a slider moved: drag released, or Left / Right */
    std::function<void()> back;                     /* Esc, Left off a slider */
};

class Roller {
public:
    Roller(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, RollerEvents events);
    ~Roller();
    Roller(const Roller &) = delete;
    Roller &operator=(const Roller &) = delete;

    /* Replace the rows; `selected` is centred at once. wrap: Up on the first row goes to the last (lists). */
    void set_rows(const std::vector<RowSpec> &rows, int selected, bool wrap);
    /* Change one row's value / state in place (it keeps its place and the highlight). */
    void update_row(int index, const RowSpec &spec);
    void select(int index, bool animated);
    int selected() const { return selected_; }
    int count() const { return static_cast<int>(rows_.size()); }
    /* Hide the whole roller while another body (a form) takes the page; it keeps its rows. */
    void set_hidden(bool hidden);
    /* The object to add to the page's input group and focus: it receives the keys. */
    lv_obj_t *focus_object() const { return list_; }

private:
    struct Row {
        RowSpec spec;
        lv_obj_t *obj = nullptr;
        lv_obj_t *label = nullptr;
        lv_obj_t *value = nullptr;  /* Nav / Slider / Info / Network value text, Choice check mark */
        lv_obj_t *control = nullptr; /* switch, slider, spinner (Busy) */
        lv_obj_t *check = nullptr;  /* Network: the connected check mark */
        lv_obj_t *lock = nullptr;   /* Network: the lock (shackle and body) */
        lv_obj_t *bars[4] = {};     /* Network: signal bars */
        int distance = -1;          /* styled for this distance from the centre (-1: not yet) */
    };

    static void list_event_cb(lv_event_t *event);
    static void row_event_cb(lv_event_t *event);
    static void slider_event_cb(lv_event_t *event);
    static void body_delete_cb(lv_event_t *event);

    void build_row(int index);
    void fill_row(Row &row);
    void style_row(int index, bool force);
    void restyle_around(int previous);
    void update_chevrons();
    void set_selected(int index);
    void on_key(uint32_t key);
    void on_scroll(lv_event_code_t code);
    void on_row_clicked(int index);
    void slide_by(int index, int steps);
    int style_info(Row &row, int distance, const lv_font_t *font);
    int style_network(Row &row, int distance, const lv_font_t *value_font);

    cp0_ui_roller_page_t layout_;
    RollerEvents events_;
    lv_obj_t *body_ = nullptr;
    lv_obj_t *bar_ = nullptr;
    lv_obj_t *list_ = nullptr;
    lv_obj_t *up_ = nullptr;   /* chevrons, drawn as lines (their points live here) */
    lv_obj_t *down_ = nullptr;
    lv_point_precise_t up_points_[3]{};
    lv_point_precise_t down_points_[3]{};
    std::vector<Row> rows_;
    int selected_ = 0;
    int target_ = -1; /* centre row a programmatic scroll is heading for (-1: none) */
    bool wrap_ = true;
    bool rebuilding_ = false; /* set_rows() is replacing the rows: scroll events are ignored */
};

/* The status line's colour: green, amber (partly failed, task 014 P3d), red. */
enum class CaptionTone { Normal, Warn, Error };

class Page {
public:
    /* make_title: creates the title label at the home grid's place (native_ui::add_title in the launcher); NULL
     * draws a plain label from the same metrics. on_back: the Esc button. */
    using TitleFactory = std::function<lv_obj_t *(lv_obj_t *parent, const char *text)>;
    Page(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TitleFactory make_title, std::function<void()> on_back);
    ~Page();
    Page(const Page &) = delete;
    Page &operator=(const Page &) = delete;

    /* The title for a path ("Settings", "Screen", "DarkTime"): the whole path joined with " > " when it fits up to
     * the Esc button, else the path without its first parts, else the last part with dots. */
    void set_title(const std::vector<std::string> &path);
    /* A dialog's title in the title slot while the keyboard leaves it no room in its box (cp0_ui_dialog.h step 3):
     * the latest owner's text shows instead of the path; text NULL removes the owner's (the path comes back when none
     * is left). */
    void set_title_override(const void *owner, const char *text);
    /* One status line under the title (empty hides it); error draws it red. */
    void set_caption(const std::string &text, bool error);
    void set_caption(const std::string &text, CaptionTone tone);
    /* A status line too long for its room wraps onto the lines that fit above the centre row (at most
     * CP0_UI_ROLLER_CAPTION_MAX_LINES, cp0_ui_roller_caption_lines) instead of ending with dots on one line. */
    void set_caption_wrap(bool wrap);

    lv_obj_t *root() const { return root_; }
    lv_obj_t *header() const { return header_; }
    const cp0_ui_roller_page_t &layout() const { return layout_; }

    /* A scrollable column under the header for form rows (TextField rows), centred, roller-wide rows. Its height is
     * the body's minus the on-screen keyboard's inset, and the field that asked for the keyboard is scrolled into
     * view above it. Created on first use; clear_body() deletes it with its rows. */
    lv_obj_t *body();
    void clear_body();

private:
    static void esc_event_cb(lv_event_t *event);
    static void root_delete_cb(lv_event_t *event);
    void on_inset(int inset);
    void apply_title(const std::vector<std::string> &path);
    void show_caption();

    cp0_ui_roller_page_t layout_;
    std::function<void()> on_back_;
    std::vector<std::string> title_path_;
    std::vector<std::pair<const void *, std::string>> title_overrides_; /* dialogs' titles, latest last */
    bool caption_wrap_ = false;
    lv_obj_t *root_ = nullptr;
    lv_obj_t *header_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *caption_ = nullptr;
    lv_obj_t *esc_ = nullptr;
    lv_obj_t *body_ = nullptr;
    int inset_listener_ = 0;
};

/* ------------------------------------------------------------------ read-only pages (task 014 P3a) */

struct InfoItem {
    std::string key;
    std::string value; /* empty: the key alone across the row */
};

class InfoTable {
public:
    /* back: Esc / Left (and the page's header Esc, which the page calls itself) */
    InfoTable(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, std::function<void()> back);
    InfoTable(const InfoTable &) = delete;
    InfoTable &operator=(const InfoTable &) = delete;

    /* Replace the rows. Same keys as before: the values change in place and the highlight stays. */
    void set_items(const std::vector<InfoItem> &items);
    void set_value(int index, const std::string &value);
    int count() const { return static_cast<int>(items_.size()); }
    void set_hidden(bool hidden) { roller_.set_hidden(hidden); }
    lv_obj_t *focus_object() const { return roller_.focus_object(); }

private:
    static RowSpec spec_of(const InfoItem &item);

    Roller roller_;
    std::vector<InfoItem> items_;
};

class TextBlock {
public:
    /* font: NULL = the BODY text size of the layout service (builtin Montserrat); pages pass a FreeType face for text
     * beyond ASCII */
    TextBlock(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, const std::string &text, const lv_font_t *font,
              std::function<void()> back);
    ~TextBlock();
    TextBlock(const TextBlock &) = delete;
    TextBlock &operator=(const TextBlock &) = delete;

    lv_obj_t *focus_object() const { return box_; }
    void set_hidden(bool hidden);

private:
    static void box_event_cb(lv_event_t *event);
    void on_key(uint32_t key);

    cp0_ui_roller_page_t layout_;
    std::function<void()> back_;
    lv_obj_t *box_ = nullptr; /* scrolls; in the page's input group */
    lv_obj_t *label_ = nullptr;
};

/* ------------------------------------------------------------------ text fields and the on-screen keyboard */

enum class TextType { Text, Number, Password };

struct TextFieldSpec {
    std::string label;
    TextType type = TextType::Text;
    std::string text;           /* initial text */
    std::string placeholder;
    std::size_t max_bytes = 128; /* UTF-8 bytes */
    bool multiline = false;      /* Tab types a tab character; Enter a new line (no submit) */
    int label_w = 0; /* > 0 (task 014 P4, the SSH form): the label BESIDE the field on one row, in a column this wide, at
                        the BODY text size, the field taking the rest of the row; 0: the label above the field */
};

struct TextFieldEvents {
    std::function<void(const std::string &text)> changed;
    std::function<void(const std::string &text)> submit; /* Enter while editing (single line); default: stop editing */
    std::function<void()> back;                           /* Esc while not editing: the page's back action */
    std::function<void()> escape; /* Esc while editing (a dialog's Cancel); default: stop editing. Runs inside the key
                                     event: delete the field later, not in the callback */
};

class TextField {
public:
    TextField(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, TextFieldSpec spec, TextFieldEvents events);
    ~TextField();
    TextField(const TextField &) = delete;
    TextField &operator=(const TextField &) = delete;

    const std::string &text() const { return text_; }
    void set_text(const std::string &text);
    bool editing() const { return editing_; }
    void begin_edit();
    void end_edit();
    bool revealed() const { return revealed_; }
    void set_revealed(bool revealed); /* Password: show the text (the toggle inside the field) */
    /* Overwrite the text in memory and clear the field (a Password's text is zeroed again when the field is deleted).
     * Editing, if any, goes on. */
    void wipe();

    lv_obj_t *row() const { return row_; }
    /* The field: add it to the page's input group (focus starts editing). */
    lv_obj_t *focus_object() const { return box_; }

private:
    static void box_event_cb(lv_event_t *event);
    static void eye_event_cb(lv_event_t *event);
    static void keyboard_event_cb(lv_event_t *event);
    static void row_delete_cb(lv_event_t *event);
    static void screen_delete_cb(lv_event_t *event);

    void on_key(const struct key_item &item);
    void insert(const char *utf8);
    void erase_before();
    void erase_after();
    void move_cursor(int delta);
    void refresh();
    void style_editing();
    void focus_step(bool next);
    void stop(bool silent);

    cp0_ui_roller_page_t layout_;
    TextFieldSpec spec_;
    TextFieldEvents events_;
    lv_obj_t *row_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_obj_t *box_ = nullptr;
    lv_obj_t *area_ = nullptr; /* lv_textarea, display only (not clickable, not in a group) */
    lv_obj_t *eye_ = nullptr;
    lv_obj_t *eye_label_ = nullptr;
    std::string text_;
    std::size_t cursor_ = 0; /* byte offset in text_ */
    bool editing_ = false;
    bool revealed_ = false;
    /* what editing replaced, restored when it stops */
    cp0_keyboard_input_context_t saved_context_ = KBD_INPUT_CONTEXT_NAVIGATION;
    int saved_intercept_ = 0;
    cp0_keyboard_text_hint_t saved_hint_ = KBD_TEXT_HINT_TEXT;
    lv_obj_t *key_screen_ = nullptr; /* the screen LV_EVENT_KEYBOARD is read on while editing */
};

/* ------------------------------------------------------------------ dialog (task 014 P3b) */

enum class ButtonRole { Normal, Primary, Cancel };

struct DialogButton {
    std::string text;
    ButtonRole role = ButtonRole::Normal; /* Primary: Enter; Cancel: Esc */
    std::function<void()> action;         /* runs inside an LVGL event: delete the dialog later, not in it */
};

struct DialogSpec {
    std::string title;
    std::vector<DialogButton> buttons; /* 1-3, left to right; a single one is pressed by Enter and by Esc */
    bool message_line = false;         /* reserve one status line at the end of the body (set_message) */
    Page *page = nullptr; /* the page under it: its header shows the title when the keyboard leaves no room (step 3) */
};

class Dialog {
public:
    /* parent: the page's full-screen root (the dialog and its backdrop cover the body under the header). */
    Dialog(lv_obj_t *parent, const cp0_ui_roller_page_t &layout, DialogSpec spec);
    ~Dialog();
    Dialog(const Dialog &) = delete;
    Dialog &operator=(const Dialog &) = delete;

    /* The content column (text, fields), the dialog's content width. Call relayout() after adding to it. */
    lv_obj_t *body() const { return content_; }
    /* The page layout narrowed to the body: give it to a TextField built in body(). */
    const cp0_ui_roller_page_t &field_layout() const { return field_layout_; }
    lv_obj_t *add_text(const std::string &text);
    void set_title(const std::string &title);
    /* The status line (message_line): empty clears it; error draws it red. */
    void set_message(const std::string &text, bool error);
    /* A button the finger or Enter must not press now (the sudo prompt while it authenticates): dimmed, no clicks. */
    void set_button_enabled(std::size_t index, bool enabled);
    void relayout();
    /* Keeps the dialog above whatever the page draws later (the page's status line). */
    void raise();

    /* A dialog without fields: add this to the page's input group and focus it (Enter = primary, Esc = cancel). */
    lv_obj_t *focus_object() const { return box_; }
    void primary();
    void cancel();
    lv_obj_t *root() const { return backdrop_; }
    const cp0_ui_dialog_t &geometry() const { return geometry_; }

private:
    static void box_event_cb(lv_event_t *event);
    static void button_event_cb(lv_event_t *event);
    static void backdrop_delete_cb(lv_event_t *event);
    void run(ButtonRole role);
    void keep_in_view();
    void title_to_header(bool in_header);

    cp0_ui_roller_page_t layout_;
    cp0_ui_roller_page_t field_layout_;
    DialogSpec spec_;
    bool title_in_header_ = false;
    cp0_ui_dialog_t geometry_{};
    lv_obj_t *backdrop_ = nullptr;
    lv_obj_t *box_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *viewport_ = nullptr; /* the body's scrolling window */
    lv_obj_t *content_ = nullptr;
    lv_obj_t *message_ = nullptr;
    std::vector<lv_obj_t *> buttons_;
    int inset_listener_ = 0;
};

class OnScreenKeyboard {
public:
    /* Builds the keyboard on `layer` (a display's top layer) at layout.y, full width, layout.height high. */
    OnScreenKeyboard(lv_obj_t *layer, const cp0_ui_osk_layout_t &layout, TextType type, bool multiline);
    /* Sends every release still owed (an Esc pressed when the keyboard goes) and deletes the objects. */
    ~OnScreenKeyboard();
    OnScreenKeyboard(const OnScreenKeyboard &) = delete;
    OnScreenKeyboard &operator=(const OnScreenKeyboard &) = delete;

    TextType type() const { return type_; }
    bool multiline() const { return multiline_; }
    int height() const { return layout_.height; }
    lv_obj_t *root() const { return root_; }
    /* A physical keyboard appeared: owed releases sent, Shift / Ctrl dropped. */
    void release_all();

private:
    struct Key {
        cp0_ui_osk_place_t place{};
        lv_obj_t *obj = nullptr;
        lv_obj_t *label = nullptr;
    };

    static void key_event_cb(lv_event_t *event);
    static void root_delete_cb(lv_event_t *event);
    static void rebuild_cb(void *data);

    void build();
    void restyle();
    void style_key(Key &key);
    void inject(const cp0_ui_osk_event_t *events, int count);
    void on_key_event(int index, lv_event_code_t code);

    cp0_ui_osk_layout_t layout_;
    TextType type_;
    bool multiline_;
    cp0_ui_osk_state_t state_{};
    lv_obj_t *root_ = nullptr;
    std::vector<Key> keys_;
    bool rebuild_armed_ = false;
};

namespace osk {

/* The host's policy (the launcher: presence, Settings Auto / Off, lock screen, desktop, APPLAUNCH_OSK): may the
 * keyboard show now? Asked when a field requests it and every 250 ms while one does. Without a policy it never shows. */
void set_policy(std::function<bool()> allowed);

/* A field started editing (field: its focus object): the keyboard shows for it, on its display, while allowed. */
void request(lv_obj_t *field, TextType type, bool multiline);
/* That field stopped editing. A release from another object than the current owner is ignored. */
void release(lv_obj_t *field);
/* Page change, app start, lock: hide at once and forget the request (the field restores its own state). */
void dismiss();

bool visible();
lv_obj_t *owner();   /* the field the keyboard is up (or wanted) for, or NULL */
int bottom_inset();  /* the keyboard's height while shown, else 0 */

/* Called with the new inset whenever it changes. Returns an id for remove_inset_listener(). */
int add_inset_listener(std::function<void(int inset)> listener);
void remove_inset_listener(int id);

/* Dialog primitive: keep obj centred vertically in the room between `top` and the keyboard (whole room when it is
 * hidden; top-aligned when it does not fit), following the keyboard until obj is deleted. */
void keep_above_keyboard(lv_obj_t *obj, int top);

} // namespace osk

} // namespace ui
} // namespace cp0
