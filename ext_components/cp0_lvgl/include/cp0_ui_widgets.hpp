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
 *             Nav     label, current value right-aligned in green (opens something); opt-in (RowSpec::chevron) a grey
 *                     chevron at the right edge, the value left of it
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
 *   CompactList        the apps' list (cp0_ui_list.h): top-aligned rows (row 0 under the area's top edge, the highlight
 *                      moves down, the list scrolls only to keep the highlight and one row of context in view, never
 *                      centred), no scroll chevrons, a thin scrollbar at the right edge while scrolling. Two densities:
 *                      Compact (dense data, 5 mm rows, about 7 on the deck) and Comfortable (the roller's row height, a few
 *                      touch-friendly rows). Rows: item (label cut with dots, value right never cut, value tone, an
 *                      optional affordance: Info / Open / Edit / Action / ActionRunning) and section header. Live
 *                      refresh in place (no flicker, no scroll jump, selection kept), read-only mode, an empty-state
 *                      line. Keys, tap and tap-again-to-activate as the Roller.
 *   Dialog             (task 014 P3b) a title, a body (text, TextFields) and 1-3 buttons: the primary one is Enter, the
 *                      cancel one Esc (also the keyboard's Enter / Esc while a field edits). Width DIALOG_MAX_W,
 *                      centred in the room between the page header and the on-screen keyboard and following the
 *                      keyboard; the body scrolls; when the keyboard leaves no room the buttons go, then the title
 *                      moves into the page header's title slot (cp0_ui_dialog.h): the header itself, its Esc button
 *                      and the status strip are never covered. A backdrop under it takes the taps meant for the page.
 *                      A notice (one button, task 014 P3c): Enter and Esc both press it.
 *
 *   StatusStrip        (task L1) the clock pill, Wi-Fi bars and Bluetooth glyph at the top right, exactly as on the
 *                      launcher's home grid (the cp0_statusbar renderer, placed and scaled by the layout service). Apps
 *                      use it as it is: never draw your own clock or icons, never copy it. add_title() is the home
 *                      grid's title label (what Page's TitleFactory makes).
 *
 * LVGL thread only. The objects are children of the parent given to the constructor; the destructor deletes them
 * (unless the parent went first).
 */
#pragma once

#include "cp0_statusbar.h"
#include "cp0_ui_dialog.h"
#include "cp0_ui_info.h"
#include "cp0_ui_list.h"
#include "cp0_ui_osk.h"
#include "cp0_ui_roller.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <cstddef>
#include <cstdint>
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
    bool chevron = false; /* Nav: a grey chevron at the right edge, the value just left of it (the row opens a picker or
                           * a page: CompactList's Open / Edit look, cp0_ui_list.h). Opt-in: false draws the row as
                           * before */
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
        lv_obj_t *chevron = nullptr; /* Nav with RowSpec::chevron */
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

/* ------------------------------------------------------------------ status strip and title (task L1) */

/* Fills the Wi-Fi and Bluetooth fields of the strip's state (wifi_up, wifi_pct, bt_on, bt_connected; the strip sets
 * the clock itself, and the battery fields from cp0_power_supply_battery() right before the source runs: a board
 * without a power_supply battery shows none). Called on the LVGL thread when the strip is added, then every 2 s while
 * the strip's screen is the active one, and by StatusStrip::refresh(). Must return quickly: start slow queries
 * elsewhere and report the last known values. cp0_ui_status_set_wifi() (cp0_ui_status_strip.h) applies the bars'
 * rule. */
using StatusSource = std::function<void(cp0_statusbar_state_t &state)>;

/* The default source: the launcher's status feed while it is alive (status.state, cp0_app_status_read: Wi-Fi, Bluetooth
 * and battery as the home grid shows them), else sysfs / procfs (cp0_statusbar_read_state: the Wi-Fi interface found
 * under any name, the hci devices and rfkill). Needs no cp0_lvgl service, works in any process. */
StatusSource default_status_source();

class StatusStrip {
public:
    /* Adds the strip at the top right of parent: a full-width screen or page header at y 0 (Page::header()). The strip
     * belongs to parent: it stops its timers and frees itself when parent is deleted (never delete it yourself). source
     * empty = default_status_source(). Returns NULL under the SDL simulator (no renderer there) or when parent is NULL.
     * Fonts: share/font/Montserrat-Medium.ttf and FontAwesome5-Solid+Brands+Regular.woff from cp0_file_path_c, else
     * /usr/share/APPLaunch/share/font (the launcher's copy; apps bundle none). */
    static StatusStrip *add(lv_obj_t *parent, StatusSource source = StatusSource());

    /* Reads the source and redraws now (when the strip's screen is the active one): call it right after loading the
     * screen so the icons are current at once. */
    void refresh();
    lv_obj_t *canvas() const { return canvas_; }
    /* Multitasking P-c (G2, the long-press on the strip): the area of a strip shown now on `display` (its canvas on the
     * active screen, from status_strip_left() to its right edge), in display coordinates; false when none is (another
     * screen, a page without the strip, the SDL simulator). The strip itself stays as it is: not clickable, a tap on it
     * does what it did. LVGL thread. */
    static bool active_area(lv_display_t *display, lv_area_t *out);

    StatusStrip(const StatusStrip &) = delete;
    StatusStrip &operator=(const StatusStrip &) = delete;

private:
    StatusStrip(lv_obj_t *parent, StatusSource source);
    static void parent_delete_cb(lv_event_t *event);
    static void clock_timer_cb(lv_timer_t *timer);
    static void status_timer_cb(lv_timer_t *timer);
    void clock_tick();
    bool screen_active() const;
    void redraw();

    lv_obj_t *parent_ = nullptr;
    lv_obj_t *canvas_ = nullptr;
    StatusSource source_;
    std::vector<uint32_t> pixels_; /* ARGB8888, status_w x max(bar_h, CP0_STATUSBAR_HEIGHT) */
    int width_ = 0, top_ = 0, pct_ = 100;
    cp0_statusbar_state_t state_{};
    cp0_statusbar_state_t shown_{};
    bool drawn_ = false;
    lv_timer_t *clock_timer_ = nullptr;
    lv_timer_t *status_timer_ = nullptr;
};

/* x of the strip's leftmost drawn pixel on the native display (its fullest state: Bluetooth glyph, all bars, a clock;
 * a battery at "100%" when the board has a power_supply battery at the first call, which fixes the value):
 * what a page header keeps free, the strip_left argument of cp0_ui_roller_page(). 0 under the SDL simulator. */
int status_strip_left();

/* The home grid's title: a label at the shell's title_x / title_y, title_px, gold (#F0B400). Use it as Page's
 * TitleFactory, or alone on a screen of your own. */
lv_obj_t *add_title(lv_obj_t *parent, const char *text);

/* The status line's colour: green, amber (partly failed, task 014 P3d), red. */
enum class CaptionTone { Normal, Warn, Error };

class Page {
public:
    /* make_title: creates the title label at the home grid's place (cp0::ui::add_title); NULL
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
    /* One status line under the title, the full content width (empty hides it); error draws it red. Too long on one
     * line: "NAME  VALUE" (two spaces) keeps VALUE whole and shortens NAME with dots, any other text ends with dots. */
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

/* ------------------------------------------------------------------ CompactList (the apps' list) */

enum class ListDensity { Compact, Comfortable };
enum class ListRowKind { Item, Section };
/* Value colour: green (the roller's), light green, amber, red; Muted: grey for an absent / unset thing
 * (never the only signal of status). */
enum class ValueTone { Normal, Ok, Warn, Bad, Muted };
/* What an item does when activated, shown by its look (cp0_ui_list.h, "Row affordances"); opt-in per row, purely
 * visual (keys and taps unchanged). Unspecified (the default) draws the row exactly as before affordances existed.
 *   Info    read-only information: no chevron; a Normal value is drawn grey (Muted), Ok / Warn / Bad / Muted as given
 *   Open    navigates to a page: a grey chevron at the right edge
 *   Edit    changes a value, in place or through a picker: the value in its tone, then the chevron
 *   Action  performs something (save, start, listen, send), available: a white dot in the chevron's slot, white label
 *   ActionRunning  that action is in progress (listening, a test running): the dot in the accent blue (#5AAAFF)
 * A chevron and a dot share one slot, so values end in one column. Switch Action <-> ActionRunning with set_rows() /
 * update_row(): only the dot's colour changes. Section rows ignore it. */
enum class RowAffordance { Unspecified, Info, Open, Edit, Action, ActionRunning };

struct ListRow {
    ListRowKind kind = ListRowKind::Item;
    std::string label;                 /* left, one line, cut with dots; a Section's title (drawn upper case, grey) */
    std::string value;                 /* right, never cut; empty: none (the label takes the whole row) */
    ValueTone tone = ValueTone::Normal;
    std::intptr_t id = 0;              /* the app's own identifier, given back to activate */
    RowAffordance affordance = RowAffordance::Unspecified; /* an item's chevron / dot / colours (see RowAffordance) */

    static ListRow item(std::string label, std::string value = std::string(), ValueTone tone = ValueTone::Normal,
                        std::intptr_t id = 0, RowAffordance affordance = RowAffordance::Unspecified)
    {
        ListRow row;
        row.label = std::move(label);
        row.value = std::move(value);
        row.tone = tone;
        row.id = id;
        row.affordance = affordance;
        return row;
    }
    static ListRow section(std::string title)
    {
        ListRow row;
        row.kind = ListRowKind::Section;
        row.label = std::move(title);
        return row;
    }
};

struct CompactListEvents {
    std::function<void(int index, std::intptr_t id)> activate; /* Enter, Right, a tap on the selected item. May replace
                                                                  the rows; delete the list later, not in it */
    std::function<void(int index)> selected;                    /* optional: the highlight moved (keys, tap, select()) */
    std::function<void()> back;                                 /* Esc, Left */
};

struct CompactListOptions {
    bool read_only = false;  /* no highlight, no activation: a pure scrolling list (logs, info) */
    bool wrap = false;       /* Up on the first item goes to the last, Down on the last to the first */
    std::string empty_text;  /* the grey line shown while the list has no row ("" = none) */
};

class CompactList {
public:
    /* layout: cp0_ui_list_page_layout() for a Page's body (the roller's column, under the header), or
     * cp0_ui_list_layout() for any area; set layout.separator_px = 1 first for a hairline between items. */
    CompactList(lv_obj_t *parent, const cp0_ui_list_layout_t &layout, CompactListEvents events,
                CompactListOptions options = CompactListOptions());
    ~CompactList();
    CompactList(const CompactList &) = delete;
    CompactList &operator=(const CompactList &) = delete;

    /* Live data (call it every second if you like): the rows replace the old ones in place. Only the texts and
     * colours that changed are touched; the scroll offset and the selected index stay (clamped when rows went away;
     * a selection that lands on a section moves to the next item). No flicker, no scroll jump, no focus loss. The
     * selection follows the index, not the row's identity: select() it again if your rows re-sort. */
    void set_rows(const std::vector<ListRow> &rows);
    /* New content (another page of the app): the rows, scrolled to the top, `selected` highlighted (clamped; the next
     * item when it is a section) and in view. The scrollbar shows for a second when the rows do not all fit. */
    void reset_rows(const std::vector<ListRow> &rows, int selected = 0);
    /* One row in place (same kind: only its texts / colours / affordance; another kind: the rows below move, nothing
     * scrolls). */
    void update_row(int index, const ListRow &row);
    void set_value(int index, const std::string &value, ValueTone tone = ValueTone::Normal);

    /* Highlight an item and scroll just enough to show it with one row of context. Read-only: scroll it into view. */
    void select(int index, bool animated = true);
    int selected() const { return selected_; } /* -1: none (read-only, or no item) */
    int count() const { return static_cast<int>(rows_.size()); }
    const ListRow &row(int index) const;       /* an empty row when index is out of range */

    /* The empty state: a grey line at the top of the area while the list has no row; empty_area() is the box it
     * lives in (the whole list area, shown only while there is no row): add a card of your own to it. */
    void set_empty_text(const std::string &text);
    lv_obj_t *empty_area() const { return empty_; }

    void set_hidden(bool hidden);
    /* The object to add to the page's input group and focus: it receives the keys (LV_EVENT_KEY). */
    lv_obj_t *focus_object() const { return list_; }
    const cp0_ui_list_layout_t &layout() const { return layout_; }

private:
    struct Row {
        ListRow spec;
        lv_obj_t *obj = nullptr;
        lv_obj_t *label = nullptr;
        lv_obj_t *value = nullptr;
        lv_obj_t *chevron = nullptr; /* Open / Edit: made the first time the row needs it, hidden otherwise */
        lv_obj_t *dot = nullptr;     /* Action / ActionRunning: the same */
        bool styled_selected = false;
    };

    static void list_event_cb(lv_event_t *event);
    static void row_event_cb(lv_event_t *event);
    static void list_delete_cb(lv_event_t *event);
    static void hide_timer_cb(lv_timer_t *timer);

    void build_row(int index);
    void style_kind(Row &row);
    void fill_row(Row &row, const ListRow *previous);
    void show_chevron(Row &row, bool shown);
    void show_dot(Row &row, cp0_ui_list_affordance_t affordance);
    void style_selected(int index);
    const lv_font_t *label_font(const Row &row) const;
    void relayout();
    void apply(const cp0_ui_list_result_t &result, bool animated, bool notify);
    void set_selected(int index, bool notify);
    void scroll_to(int y, bool animated);
    int scroll_base() const;
    cp0_ui_list_state_t state() const;
    void show_scrollbar();
    void update_empty();
    void on_key(uint32_t key);
    void on_row_clicked(int index);

    cp0_ui_list_layout_t layout_;
    CompactListEvents events_;
    CompactListOptions options_;
    lv_obj_t *list_ = nullptr;   /* scrolls; holds the rows and the empty area */
    lv_obj_t *empty_ = nullptr;
    lv_obj_t *empty_label_ = nullptr;
    lv_timer_t *hide_timer_ = nullptr;
    std::vector<Row> rows_;
    std::vector<cp0_ui_list_kind_t> kinds_;
    std::vector<int> tops_;       /* count + 1 */
    int selected_ = -1;
    bool busy_ = false;           /* a keyboard scroll animation is running */
    int busy_target_ = 0;         /* ... towards this offset */
    bool bar_shown_ = false;
    bool rebuilding_ = false;     /* set_rows() at work: its scroll events are not the user's */
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

/* Destructive (multitasking P-d): drawn as Normal with its label in red (#FF8A8E): an action that ends something
 * (the legacy sheet's Close); no key presses it. */
enum class ButtonRole { Normal, Primary, Cancel, Destructive };

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
int bottom_inset();  /* the keyboard's height while shown, else 0 (at least the reserved inset below) */
/* The field that asked (owner) is a multi-line one (a terminal): Tab types a tab character there. */
bool multiline();
/* Room the host keeps free at the bottom of the display without a keyboard (the PDA writing zone while it shows, report
 * 041, 042): the inset is the larger of this and the keyboard's height, so pages, the terminal and dialogs leave it free
 * as they do for the keyboard. 0 = none (the default). */
void set_reserved_inset(int px);

/* The PDA writing zone (report 042): while zone_input() is true the zone is the text input instead of this keyboard.
 * Where the keyboard would show (a field requested it, the policy allows it, and the field's screen is the one shown),
 * show(true) is called instead and no keyboard is built; show(false) when that ends (released, dismissed, not allowed
 * any more, another screen in front, zone_input() false). Without it (the default) the keyboard works as before. */
void set_zone(std::function<bool()> zone_input, std::function<void(bool show)> show);
/* Asks the policy again on the next cycle (a screen came up over the field: the multitasking switcher). */
void reevaluate();

/* Called with the new inset whenever it changes. Returns an id for remove_inset_listener(). */
int add_inset_listener(std::function<void(int inset)> listener);
void remove_inset_listener(int id);

/* Dialog primitive: keep obj centred vertically in the room between `top` and the keyboard (whole room when it is
 * hidden; top-aligned when it does not fit), following the keyboard until obj is deleted. */
void keep_above_keyboard(lv_obj_t *obj, int top);

} // namespace osk

} // namespace ui
} // namespace cp0
