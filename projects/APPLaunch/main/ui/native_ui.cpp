/*
 * Native full-screen UI for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 */

#include "native_ui.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "app_launch_env.hpp"
#include "home_order_launcher.hpp"
#include "input_presence.hpp"
#include "launch.h"
#include "launcher_osk.hpp"
#include "launcher_pointer.hpp"
#include "launcher_ssh.hpp"
#include "launcher_toast.h"
#include "native_vkbd.hpp"
#include "settings/settings_apps_jobs.hpp"
#include "touch_settings.hpp"
#include "ui_global_hint.h"
#include "ui_screensaver.h"

#include "cp0_display.h"
#include "cp0_font_service.hpp"
#include "cp0_statusbar.h"
#include "cp0_ui_status_strip.h"
#include "cp0_ui_widgets.hpp"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_overlay.h"
#include "hal_lvgl_bsp.h"
#include "cp0_esc_state.h"
#include "input_keys.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/draw/lv_image_decoder_private.h"   // lv_image_cache_drop
#include "sample_log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "applaunch_vfb.h"
#include "cp0_lvgl_app.h"
#include "cp0_lvgl_app_runner.hpp"

namespace {

constexpr uint32_t kBg = 0x000000;
constexpr uint32_t kTileBg = 0x1E1E1E;
constexpr uint32_t kTileBorder = 0x3A3A3A;
constexpr uint32_t kAccent = 0x2D9CDB;

Launch *s_launch = nullptr;
std::string s_launching_app; // display name of the app being launched

lv_obj_t *s_home = nullptr;     // native home screen (native display)
lv_obj_t *s_grid = nullptr;
lv_obj_t *s_chrome = nullptr;   // toolbar screen shown in compat mode (native display)
lv_obj_t *s_idle = nullptr;     // empty screen parked on the compat display while at home
std::vector<lv_obj_t *> s_tiles;
int s_selected = 0;
// Multitasking P-d: the tiles' "left open" dots (made the first time a tile needs one) and the apps they mark
std::vector<lv_obj_t *> s_dots;
std::vector<std::string> s_resident_keys;

// Geometry of the native screens, from the layout service (cp0_ui_metrics.h) for the native display. The
// 640x480 deck and the 480x320 Pi 3A+ are pinned presets: the values the UI always had (they must not
// change; the render harness goldens check it). Other sizes keep today's two layouts (deck from 400 px of
// height, compact below: status bar 40, padding 10, icon and text about 72 % of the deck sizes) for the strip,
// title and toolbar; the home grid of every other size is computed from the physical tokens (task 014 P1c).
struct Layout {
    bool compact;
    int screen_w, screen_h;
    int cols, rows;      // grid columns, rows that fit on screen
    int bar_h;           // status bar height
    int pad;             // grid padding and gap
    int tile_w, tile_h;  // two rows fit exactly under the status bar (computed sizes: see cp0_ui_metrics.c)
    int top_extra;       // extra space above the first row (grid centred vertically; 0 on the pinned presets)
    int tile_radius, tile_border, tile_border_sel;
    int icon, icon_top;
    const lv_font_t *label_font;
    int label_bottom, label_inset;
    int tb_pad_x, tb_pad_y, tb_gap, tb_radius;
    const lv_font_t *tb_text_font, *tb_symbol_font;
    int toolbar_y, toolbar_h; // stock-app toolbar under the compat window, full width
};

const Layout &layout()
{
    static Layout l = [] {
        const cp0_ui_metrics_t &m = *cp0_ui_metrics_get();
        const cp0_ui_shell_t &s = m.shell;
        Layout v{};
        v.compact = m.shell_preset == CP0_UI_PRESET_COMPACT;
        v.screen_w = m.w;
        v.screen_h = m.h;
        v.cols = s.cols;
        v.rows = s.rows;
        v.bar_h = s.bar_h;
        v.pad = s.pad;
        v.tile_w = s.tile_w;
        v.tile_h = s.tile_h;
        v.top_extra = s.top_extra;
        v.tile_radius = s.tile_radius;
        v.tile_border = s.tile_border;
        v.tile_border_sel = s.tile_border_sel;
        v.icon = s.icon;
        v.icon_top = s.icon_top;
        v.label_font = cp0_ui_font_px(s.label_px);
        v.label_bottom = s.label_bottom;
        v.label_inset = s.label_inset;
        v.tb_pad_x = s.tb_pad_x;
        v.tb_pad_y = s.tb_pad_y;
        v.tb_gap = s.tb_gap;
        v.tb_radius = s.tb_radius;
        v.tb_text_font = cp0_ui_font_px(s.tb_text_px);
        v.tb_symbol_font = cp0_ui_font_px(s.tb_symbol_px);
        v.toolbar_y = m.compat.toolbar_y;
        v.toolbar_h = m.compat.toolbar_h;
        return v;
    }();
    return l;
}

// Creates objects on a specific display regardless of the current default.
class DefaultDisplayScope
{
public:
    explicit DefaultDisplayScope(lv_display_t *display) : previous_(lv_display_get_default())
    {
        lv_display_set_default(display);
    }
    ~DefaultDisplayScope() { lv_display_set_default(previous_); }
    DefaultDisplayScope(const DefaultDisplayScope &) = delete;
    DefaultDisplayScope &operator=(const DefaultDisplayScope &) = delete;

private:
    lv_display_t *previous_;
};

lv_obj_t *make_black_screen()
{
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_radius(screen, 0, 0);
    return screen;
}

// ------------------------------------------------------------------ home

void select_tile(int index, bool scroll)
{
    if (s_tiles.empty()) return;
    const int count = static_cast<int>(s_tiles.size());
    index = ((index % count) + count) % count;
    if (s_selected >= 0 && s_selected < count)
        lv_obj_remove_state(s_tiles[s_selected], LV_STATE_CHECKED);
    s_selected = index;
    lv_obj_add_state(s_tiles[s_selected], LV_STATE_CHECKED);
    if (scroll) lv_obj_scroll_to_view(s_tiles[s_selected], LV_ANIM_ON);
}

void launch_tile(int index)
{
    if (!s_launch || index < 0) return;
    select_tile(index, false);
    s_launch->launch_index(static_cast<std::size_t>(index));
}

// Move mode (study 046 P2), defined after the tiles below: the tiles' touch events (a tap launches, as before) and the
// keys it owns while a tile is picked up (true: the key is used)
void tile_event_cb(lv_event_t *event);
bool move_key(const struct key_item &item, uint32_t code);
void move_update_outline();

uint32_t nav_alias(uint32_t key)
{
    switch (key) {
    case KEY_F: return KEY_UP;
    case KEY_X: return KEY_DOWN;
    case KEY_Z: return KEY_LEFT;
    case KEY_C: return KEY_RIGHT;
    default: return key;
    }
}

void home_key_cb(lv_event_t *event)
{
    auto *item = static_cast<struct key_item *>(lv_event_get_param(event));
    if (!item) return;
    if (item->input_context == KBD_INPUT_CONTEXT_TEXT ||
        cp0_keyboard_get_input_context() == KBD_INPUT_CONTEXT_TEXT)
        return;

    const uint32_t code = item->semantic_key ? item->semantic_key : nav_alias(item->key_code);
    if (move_key(*item, code)) return;
    if (item->key_state) {
        switch (code) {
        case KEY_LEFT: select_tile(s_selected - 1, true); break;
        case KEY_RIGHT: select_tile(s_selected + 1, true); break;
        case KEY_UP: select_tile(s_selected - layout().cols, true); break;
        case KEY_DOWN: select_tile(s_selected + layout().cols, true); break;
        default: break;
        }
    } else if (code == KEY_ENTER) {
        launch_tile(s_selected);
    }
}

// The home grid's status strip: the shared cp0::ui::StatusStrip (cp0_lvgl) fed by the launcher's own services
// (native_ui::fill_status); refreshed when the home grid is shown.
cp0::ui::StatusStrip *s_home_strip = nullptr;

// The home header has no title word (rebranding plan 025, decision 3): the status strip only. The pages keep their
// title at the shell's title origin (add_title()).
void build_status_bar(lv_obj_t *parent)
{
    s_home_strip = cp0::ui::StatusStrip::add(parent, native_ui::fill_status);
}

void ensure_home()
{
    if (s_home) return;
    DefaultDisplayScope scope(cp0_display_native());

    s_home = make_black_screen();
    lv_obj_add_event_cb(s_home, home_key_cb, static_cast<lv_event_code_t>(LV_EVENT_KEYBOARD), nullptr);
    // another screen (Settings, a page, the switcher, the toolbar of a stock app): a tile being moved goes back
    lv_obj_add_event_cb(s_home, [](lv_event_t *) { native_ui::cancel_move_mode(); }, LV_EVENT_SCREEN_UNLOAD_START,
                        nullptr);
    build_status_bar(s_home);

    s_grid = lv_obj_create(s_home);
    lv_obj_remove_style_all(s_grid);
    const Layout &l = layout();
    lv_obj_set_size(s_grid, LV_PCT(100), l.screen_h - l.bar_h);
    lv_obj_set_pos(s_grid, 0, l.bar_h);
    lv_obj_set_style_pad_all(s_grid, l.pad, 0);
    lv_obj_set_style_pad_top(s_grid, l.pad + l.top_extra, 0);
    lv_obj_set_style_pad_row(s_grid, l.pad, 0);
    lv_obj_set_style_pad_column(s_grid, l.pad, 0);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(s_grid, LV_SCROLL_SNAP_START);
    lv_obj_set_scrollbar_mode(s_grid, LV_SCROLLBAR_MODE_OFF);
    // move mode (study 046 P2): the carried tile's outline follows its visibility while the grid scrolls
    lv_obj_add_event_cb(s_grid, [](lv_event_t *) { move_update_outline(); }, LV_EVENT_SCROLL, nullptr);
}

void build_tile(int index, const app &item)
{
    const Layout &l = layout();
    const int width = l.tile_w;

    lv_obj_t *tile = lv_obj_create(s_grid);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    // no LV_OBJ_FLAG_SCROLL_ON_FOCUS: a press gives the tile LVGL's click focus, whose scroll-to-view would snap its row
    // to the top (grid snap) the moment the finger lands, during the pick-up hold too. The keyboard scrolls through
    // select_tile() (scroll_to_view), not through focus.
    lv_obj_set_size(tile, width, l.tile_h);
    lv_obj_set_style_radius(tile, l.tile_radius, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(kTileBg), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, l.tile_border, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(kTileBorder), 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    // selected (keyboard focus)
    lv_obj_set_style_border_color(tile, lv_color_hex(kAccent), LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tile, l.tile_border_sel, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x262626), LV_STATE_CHECKED);
    // pressed (touch)
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x33414D), LV_STATE_PRESSED);
    // a tap launches (CLICKED); the press, its moves and its end time the 3 s pick-up hold (study 046 P2)
    void *const user_data = reinterpret_cast<void *>(static_cast<intptr_t>(index));
    for (const lv_event_code_t code :
         {LV_EVENT_CLICKED, LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(tile, tile_event_cb, code, user_data);

    lv_obj_t *icon = lv_image_create(tile);
    lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(icon, l.icon, l.icon);
    lv_image_set_inner_align(icon, LV_IMAGE_ALIGN_STRETCH);
    // lv_image keeps the file's width/height from set_src and derives the STRETCH scale from them; a replaced
    // file must not be matched with a cached header or stale geometry, so drop LVGL's entries for the path first
    lv_image_cache_drop(item.Icon.c_str());
    lv_image_set_src(icon, item.Icon.c_str());
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, l.icon_top);

    lv_obj_t *label = lv_label_create(tile);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(label, width - l.label_inset);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, l.label_font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(label, item.Name.c_str());
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -l.label_bottom);

    s_tiles.push_back(tile);
    s_dots.push_back(nullptr);
}

// Multitasking P-d: a green dot at the tile's top right for an app left open (mockup: 16 px with a 3 px ring of the
// tile's colour, 12 px inside the tile's border on the deck; 1.4 mm and 1.1 mm elsewhere). Tiles without one have no
// object at all.
constexpr uint32_t kResidentDot = 0x35C25A;
constexpr int kResidentRing = 3;

void apply_resident_dots()
{
    if (!s_launch) return;
    const Layout &l = layout();
    const cp0_ui_metrics_t &m = *cp0_ui_metrics_get();
    for (std::size_t i = 0; i < s_tiles.size() && i < s_dots.size(); ++i) {
        const app *item = s_launch->app_at(i);
        const bool open = item && !item->Exec.empty() &&
                          std::find(s_resident_keys.begin(), s_resident_keys.end(), item->Exec) != s_resident_keys.end();
        lv_obj_t *&dot = s_dots[i];
        if (!open) {
            if (dot) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        if (!dot) {
            const int size = cp0_ui_mm_px(m.ppmm_x100, 14, 10) + 2 * kResidentRing;
            const int inset = l.tile_border + cp0_ui_mm_px(m.ppmm_x100, 11, 6) - kResidentRing;
            dot = lv_obj_create(s_tiles[i]);
            lv_obj_remove_style_all(dot);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_size(dot, size, size);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(kResidentDot), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(kTileBg), 0);
            lv_obj_set_style_border_width(dot, kResidentRing, 0);
            lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
            lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -inset, inset);
        }
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

// ------------------------------------------------------------ move mode (study 046 P2)
//
// A tile is picked up (touch: held 3 s while a bar fills under it; keys: Ctrl+O, the options key, on the selected tile;
// the trackpoint's held click sends Ctrl+O), moved (arrows: one slot or one row, clamped; a tap on another tile puts it
// there and drops it), dropped (Enter, a tap on the picked tile) or put back (Esc). While it is carried only the LVGL
// children move (lv_obj_move_to_index): s_tiles, s_dots and the tiles' captured indices keep the app list's order, and
// home_move::Session holds the grid order, until the drop rebuilds the grid. No animation: one reflow per step and a
// bar of a few pixels every 100 ms during the hold (the Pi Zero 2 W).

constexpr uint32_t kHoldShowMs = 500;  // the bar shows (the apps' hold cue: HoldTracker kShowMs)
constexpr uint32_t kHoldFireMs = 3000; // the pick-up (the bar fills over the whole hold)
constexpr uint32_t kHoldTickMs = 100;
constexpr int kHoldSlopPx = 10;        // LVGL's scroll limit (lv_indev.c LV_INDEV_DEF_SCROLL_LIMIT; cp0 keeps it)
constexpr int kPickedOutlinePx = 3;
constexpr uint32_t kMoveGlyph = 0xF0B2; // "arrows-alt" of the status strip's icon font
constexpr const char *kMoveGlyphUtf8 = "\xEF\x82\xB2";
constexpr const char *kIconFont = "FontAwesome5-Solid+Brands+Regular.woff";
constexpr const char *kMoveCaptionKeys = "Move: arrows or tap a spot  -  Enter drop  -  Esc cancel";
constexpr const char *kMoveCaptionTouch = "Tap a spot to place the app";
constexpr const char *kMoveCannotSave = "Cannot save the order (settings full)";

struct TouchHold {
    lv_timer_t *timer = nullptr;
    lv_indev_t *indev = nullptr;
    int index = -1;            // the pressed tile (its app index)
    lv_point_t start{};
    uint32_t t0 = 0;
    lv_obj_t *track = nullptr; // the bar under the tile, made when it shows
    lv_obj_t *fill = nullptr;
    int track_w = 0, track_h = 0;
};
TouchHold s_hold;
bool s_swallow_click = false; // the release of a hold whose bar showed (or that picked the tile up) is not a tap

struct MoveMode {
    bool active = false;
    home_move::Session session; // order[slot] = app index; picked() = the carried tile's app index
    lv_obj_t *badge = nullptr;  // on the carried tile
    lv_obj_t *caption = nullptr;
    bool outline = false;       // the carried tile's outline is drawn (only while the tile is wholly in the grid)
    int32_t scroll_y = 0;       // the grid's view at the pick-up: Esc gives it back with the order
};
MoveMode s_move;

// The carried tile's outline, only while the tile is wholly inside the grid's area: a tile scrolled just out of it
// would leave a sliver of outline in the grid's padding (under the caption). Called after every move and on every
// scroll of the grid (a swipe, the view following).
void move_update_outline()
{
    if (!s_move.active || s_move.session.picked() >= s_tiles.size()) return;
    lv_obj_t *tile = s_tiles[s_move.session.picked()];
    lv_area_t t, g;
    lv_obj_get_coords(tile, &t);
    lv_obj_get_coords(s_grid, &g);
    const bool inside = t.y1 >= g.y1 && t.y2 <= g.y2 && t.x1 >= g.x1 && t.x2 <= g.x2;
    if (inside == s_move.outline) return;
    s_move.outline = inside;
    lv_obj_set_style_outline_width(tile, inside ? kPickedOutlinePx : 0, 0);
}

// Move mode only: the view follows the carried tile by the least scroll that shows it whole inside the grid's padding
// (the outline included), and never past the content: no blank space under the last row nor above the first (a press
// or a snap may have left some: it goes). Not lv_obj_scroll_to_view(): with the grid's snap it puts the tile's row at
// the top, even past the last row (what select_tile() keeps doing for the normal navigation). Returns the scroll asked
// for (> 0: the content moves down).
int32_t move_keep_in_view(lv_obj_t *tile, lv_anim_enable_t anim)
{
    if (!tile || !s_grid) return 0;
    lv_obj_update_layout(s_grid);
    lv_area_t t, g;
    lv_obj_get_coords(tile, &t);
    lv_obj_get_coords(s_grid, &g);
    const int32_t top = g.y1 + lv_obj_get_style_pad_top(s_grid, LV_PART_MAIN);
    const int32_t bottom = g.y2 - lv_obj_get_style_pad_bottom(s_grid, LV_PART_MAIN);
    int32_t dy = 0; // > 0: the content moves down
    if (t.y1 < top) dy = top - t.y1;
    else if (t.y2 > bottom) dy = bottom - t.y2;
    const int32_t above = lv_obj_get_scroll_top(s_grid), below = lv_obj_get_scroll_bottom(s_grid);
    const int32_t lo = -below, hi = above; // the range that keeps the content filling the view
    dy = lo > hi ? hi : std::clamp(dy, lo, hi); // content shorter than the view: its first row at the top
    if (dy != 0) lv_obj_scroll_by(s_grid, 0, dy, anim);
    return dy;
}

void hold_stop()
{
    if (s_hold.timer) lv_timer_delete(s_hold.timer);
    if (s_hold.track) lv_obj_delete(s_hold.track);
    s_hold = TouchHold{};
}

// The finger is still down on the tile, has not moved past the scroll limit and nothing scrolls (the three-finger
// switcher gesture's own swallow ends the press with PRESS_LOST).
bool hold_still()
{
    if (!s_hold.indev || s_hold.index < 0 || s_hold.index >= static_cast<int>(s_tiles.size())) return false;
    if (lv_indev_get_state(s_hold.indev) != LV_INDEV_STATE_PRESSED) return false;
    if (lv_indev_get_scroll_obj(s_hold.indev)) return false;
    lv_point_t point;
    lv_indev_get_point(s_hold.indev, &point);
    return std::abs(point.x - s_hold.start.x) <= kHoldSlopPx && std::abs(point.y - s_hold.start.y) <= kHoldSlopPx;
}

// The bar: under the pressed tile, in the middle of the gap below it (the grid's padding under the last row), the
// tile's width less half its corner radius each side; the grid's border grey, filling with the accent.
void hold_show(uint32_t elapsed)
{
    const Layout &l = layout();
    if (!s_hold.track) {
        lv_area_t area;
        lv_obj_get_coords(s_tiles[s_hold.index], &area);
        const int inset = l.tile_radius / 2;
        s_hold.track_h = std::max(4, l.pad / 3);
        s_hold.track_w = std::max(2 * s_hold.track_h, static_cast<int>(lv_area_get_width(&area)) - 2 * inset);
        const int y = std::min(static_cast<int>(area.y2) + 1 + (l.pad - s_hold.track_h) / 2, l.screen_h - s_hold.track_h);
        s_hold.track = lv_obj_create(s_home);
        lv_obj_remove_style_all(s_hold.track);
        lv_obj_remove_flag(s_hold.track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_hold.track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_hold.track, s_hold.track_w, s_hold.track_h);
        lv_obj_set_pos(s_hold.track, area.x1 + inset, y);
        lv_obj_set_style_radius(s_hold.track, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_hold.track, lv_color_hex(kTileBorder), 0);
        lv_obj_set_style_bg_opa(s_hold.track, LV_OPA_COVER, 0);
        s_hold.fill = lv_obj_create(s_hold.track);
        lv_obj_remove_style_all(s_hold.fill);
        lv_obj_remove_flag(s_hold.fill, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(s_hold.fill, 0, 0);
        lv_obj_set_style_radius(s_hold.fill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_hold.fill, lv_color_hex(kAccent), 0);
        lv_obj_set_style_bg_opa(s_hold.fill, LV_OPA_COVER, 0);
    }
    const uint32_t done = std::min(elapsed, kHoldFireMs);
    const int width = static_cast<int>(static_cast<int64_t>(s_hold.track_w) * done / kHoldFireMs);
    lv_obj_set_size(s_hold.fill, std::max(s_hold.track_h, width), s_hold.track_h);
}

// Whether a physical keyboard is there, by the fallback keyboard's rule (input_presence, cp0_app_osk_rule: a Bluetooth
// keyboard that went to sleep still counts). No presence watcher: assumed there (the caption names the keys).
bool keyboard_present()
{
    const input_presence::KeyboardSnapshot k = input_presence::keyboards();
    if (!k.known) return true;
    return cp0_app_osk_rule(CP0_APP_VKB_AUTO, k.keyboards, k.now_s, k.any_gone_s, k.bt_gone_s) == 0;
}

// The caption takes the status line while a tile is carried (the strip is hidden meanwhile): the tiles fill the panel
// below the bar on the deck, so a bottom bar would cover a row. The caption style of the layout service, wrapped to
// two lines where it is narrow (portrait).
void show_move_caption(bool on)
{
    if (s_home_strip && s_home_strip->canvas()) {
        if (on) lv_obj_add_flag(s_home_strip->canvas(), LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(s_home_strip->canvas(), LV_OBJ_FLAG_HIDDEN);
    }
    if (!on) {
        if (s_move.caption) lv_obj_add_flag(s_move.caption, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const Layout &l = layout();
    const cp0_ui_metrics_t &m = *cp0_ui_metrics_get();
    const lv_font_t *font = cp0_ui_font_px(m.text_px[CP0_TEXT_CAPTION]);
    const int width = l.screen_w - 2 * m.shell.title_x;
    if (!s_move.caption) {
        s_move.caption = lv_label_create(s_home);
        lv_obj_set_width(s_move.caption, width);
        lv_label_set_long_mode(s_move.caption, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_font(s_move.caption, font, 0);
        lv_obj_set_style_text_color(s_move.caption, lv_color_white(), 0);
    }
    // too wide for one line (portrait): the line breaks after "tap a spot" rather than wherever the wrap falls
    std::string text = keyboard_present() ? kMoveCaptionKeys : kMoveCaptionTouch;
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const std::size_t dash = text.find("  -  ");
    if (size.x > width && dash != std::string::npos) text.replace(dash, 5, "\n");
    lv_label_set_text(s_move.caption, text.c_str());
    lv_obj_remove_flag(s_move.caption, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(s_move.caption);
    lv_obj_set_pos(s_move.caption, m.shell.title_x,
                   std::max(0, (l.bar_h - static_cast<int>(lv_obj_get_height(s_move.caption))) / 2));
}

// The status strip's icon font file, found as the strip finds it (cp0_ui_status_strip_widget.cpp font_path): the
// program's share/font, else the launcher's installed copy. The resource paths resolve only .ttf / .otf as fonts
// (cp0_resource_path_policy.cpp), so a bare .woff name would not be found.
std::string icon_font_path()
{
    const std::string rel = std::string("share/font/") + kIconFont;
    const char *own = cp0_file_path_c(rel.c_str());
    if (own && own[0] && ::access(own, R_OK) == 0) return own;
    return "/usr/share/APPLaunch/" + rel;
}

// The move badge at the carried tile's top right: the resident dot's place and ring, in the accent, with the
// four-arrow glyph of the status strip's icon font (FontAwesome 5); without that font, the builtin left / right arrows.
lv_obj_t *make_move_badge(lv_obj_t *tile)
{
    const Layout &l = layout();
    const cp0_ui_metrics_t &m = *cp0_ui_metrics_get();
    const int size = cp0_ui_mm_px(m.ppmm_x100, 30, 22) + 2 * kResidentRing;
    const int inset = l.tile_border + cp0_ui_mm_px(m.ppmm_x100, 11, 6) - kResidentRing;
    lv_obj_t *badge = lv_obj_create(tile);
    lv_obj_remove_style_all(badge);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(badge, size, size);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(kAccent), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(badge, lv_color_hex(kTileBg), 0);
    lv_obj_set_style_border_width(badge, kResidentRing, 0);
    lv_obj_set_style_border_opa(badge, LV_OPA_COVER, 0);
    lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -inset, inset);

    const int px = (size - 2 * kResidentRing) * 3 / 5;
    lv_obj_t *glyph = lv_label_create(badge);
    lv_font_t *font = cp0_fonts().get(icon_font_path().c_str(), static_cast<uint16_t>(px));
    lv_font_glyph_dsc_t dsc;
    if (font && lv_font_get_glyph_dsc(font, &dsc, kMoveGlyph, 0) && !dsc.is_placeholder) {
        lv_obj_set_style_text_font(glyph, font, 0);
        lv_label_set_text(glyph, kMoveGlyphUtf8);
    } else {
        lv_obj_set_style_text_font(glyph, cp0_ui_font_px(px), 0);
        lv_label_set_text(glyph, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT);
    }
    lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
    lv_obj_center(glyph);
    return badge;
}

void move_begin(int index)
{
    if (!s_launch || index < 0 || index >= static_cast<int>(s_tiles.size())) return;
    s_move.active = true;
    s_move.session = home_move::Session(s_tiles.size(), static_cast<std::size_t>(index));
    lv_obj_t *tile = s_tiles[static_cast<std::size_t>(index)];
    // the selection follows the carried app; already on it (Ctrl+O), its state is left alone: a CHECKED state set
    // again re-aligns the tile's icon and label by the border's 2 px, and Esc would not give the same picture back
    if (index != s_selected) select_tile(index, false);
    lv_obj_set_style_outline_color(tile, lv_color_hex(kAccent), 0);
    lv_obj_set_style_outline_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_pad(tile, 0, 0);
    s_move.outline = false;
    s_move.badge = make_move_badge(tile);
    show_move_caption(true);
    const int32_t dy = move_keep_in_view(tile, LV_ANIM_ON);
    s_move.scroll_y = lv_obj_get_scroll_y(s_grid) - dy; // where the view settles (the scroll may still be animating)
    move_update_outline();
}

// The carried tile's look goes; the status strip comes back. Move mode itself is left to the caller.
void move_clear_look()
{
    if (s_move.badge) lv_obj_delete(s_move.badge);
    s_move.badge = nullptr;
    const std::size_t picked = s_move.session.picked();
    if (picked < s_tiles.size()) lv_obj_set_style_outline_width(s_tiles[picked], 0, 0);
    s_move.outline = false;
    show_move_caption(false);
}

void move_step(home_move::Dir dir)
{
    if (!s_move.session.step(dir, static_cast<std::size_t>(std::max(1, layout().cols)))) return;
    lv_obj_t *tile = s_tiles[s_move.session.picked()];
    lv_obj_move_to_index(tile, static_cast<int32_t>(s_move.session.slot()));
    move_keep_in_view(tile, LV_ANIM_ON);
    move_update_outline();
}

// Esc and every interruption: the tiles back in the app list's order (s_tiles never changed), the selection on the
// carried app.
void move_cancel()
{
    if (!s_move.active) return;
    move_clear_look();
    s_move.active = false;
    if (s_move.session.moved())
        for (std::size_t i = 0; i < s_tiles.size(); ++i) lv_obj_move_to_index(s_tiles[i], static_cast<int32_t>(i));
    const std::size_t picked = s_move.session.picked();
    s_move.session = home_move::Session();
    // the view of the pick-up back (the arrows may have scrolled the grid), the tile in it
    lv_obj_update_layout(s_grid);
    lv_obj_scroll_to_y(s_grid, s_move.scroll_y, LV_ANIM_OFF);
    if (picked < s_tiles.size()) move_keep_in_view(s_tiles[picked], LV_ANIM_OFF);
}

// Enter, a tap, a reload: the tile stays where it is. A move is saved once (launcher_home_order_save); the app list
// takes the new order only when it was saved, otherwise "cannot save" and the grid stays in the stored order.
// rebuild: rebuild the grid now (refresh_apps(), since the tiles capture their index and the dots are placed per
// index); a reload rebuilds it itself.
void move_drop(bool rebuild)
{
    if (!s_move.active) return;
    move_clear_look();
    s_move.active = false;
    const home_move::Session session = s_move.session;
    s_move.session = home_move::Session();
    if (!session.moved()) return;
    bool saved = false;
    if (s_launch && session.count() == s_launch->app_count()) {
        std::list<app> reordered = s_launch->apps();
        saved = home_move::apply(reordered, session.order()) && launcher_home_order_save(reordered);
    }
    if (saved) {
        s_launch->reorder_apps(session.order());
        s_selected = static_cast<int>(session.slot());
    } else {
        launcher_toast().show(kMoveCannotSave);
        s_selected = static_cast<int>(session.picked());
    }
    if (!rebuild) return;
    native_ui::refresh_apps();
    if (s_selected >= 0 && s_selected < static_cast<int>(s_tiles.size()))
        move_keep_in_view(s_tiles[static_cast<std::size_t>(s_selected)], LV_ANIM_OFF);
}

void hold_tick(lv_timer_t *)
{
    if (!hold_still()) {
        hold_stop();
        return;
    }
    const uint32_t elapsed = lv_tick_elaps(s_hold.t0);
    if (elapsed >= kHoldFireMs) {
        const int index = s_hold.index;
        hold_stop();
        s_swallow_click = true; // the finger is still on the tile: its release must not launch nor drop it
        move_begin(index);
        return;
    }
    if (elapsed >= kHoldShowMs) hold_show(elapsed);
}

void hold_start(int index)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    s_hold.indev = indev;
    s_hold.index = index;
    lv_indev_get_point(indev, &s_hold.start);
    s_hold.t0 = lv_tick_get();
    s_hold.timer = lv_timer_create(hold_tick, kHoldTickMs, nullptr);
}

void tile_event_cb(lv_event_t *event)
{
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        s_swallow_click = false;
        hold_stop();
        if (!s_move.active) hold_start(index);
        break;
    case LV_EVENT_PRESSING:
        if (s_hold.index == index && !hold_still()) hold_stop();
        break;
    case LV_EVENT_RELEASED:
        // let go after the bar showed: the hold is given up, and that is not a tap either
        if (s_hold.index == index && lv_tick_elaps(s_hold.t0) >= kHoldShowMs) s_swallow_click = true;
        hold_stop();
        break;
    case LV_EVENT_PRESS_LOST:
        // the press was taken away (the three-finger gesture's swallow ends it so; a scroll does not): a hold in
        // progress ends, a carried tile goes back
        hold_stop();
        move_cancel();
        break;
    case LV_EVENT_CLICKED:
        // LVGL 9.5 still sends CLICKED after a long press (only SHORT_CLICKED is skipped): swallowed here
        if (s_swallow_click) {
            s_swallow_click = false;
            break;
        }
        if (!s_move.active) {
            launch_tile(index);
            break;
        }
        // move mode: the carried tile goes to the tapped tile's slot (the carried tile itself: where it is), dropped
        s_move.session.place(static_cast<std::size_t>(index));
        move_drop(true);
        break;
    default:
        break;
    }
}

bool move_key(const struct key_item &item, uint32_t code)
{
    if (!s_move.active) {
        if (item.key_state != KBD_KEY_PRESSED || item.key_code != KEY_O || (item.mods & KBD_MOD_CTRL) == 0) return false;
        hold_stop();
        move_begin(s_selected);
        return true;
    }
    if (item.key_state == KBD_KEY_RELEASED) {
        if (code == KEY_ENTER) move_drop(true);
        return true;
    }
    switch (code) { // pressed or repeated
    case KEY_LEFT: move_step(home_move::Dir::Left); break;
    case KEY_RIGHT: move_step(home_move::Dir::Right); break;
    case KEY_UP: move_step(home_move::Dir::Up); break;
    case KEY_DOWN: move_step(home_move::Dir::Down); break;
    case KEY_ESC:
        if (item.key_state == KBD_KEY_PRESSED) move_cancel();
        break;
    default: break;
    }
    return true;
}

// --------------------------------------------------------------- toolbar

struct ToolbarKey {
    const char *text;
    bool symbol;    // drawn with the larger symbol font
    uint32_t code;
    bool down;
};

ToolbarKey s_keys[] = {
    {"Esc", false, KEY_ESC, false},
    {LV_SYMBOL_LEFT, true, KEY_LEFT, false},
    {LV_SYMBOL_UP, true, KEY_UP, false},
    {LV_SYMBOL_DOWN, true, KEY_DOWN, false},
    {LV_SYMBOL_RIGHT, true, KEY_RIGHT, false},
    {LV_SYMBOL_NEW_LINE, true, KEY_ENTER, false},
};

bool external_running(); // defined with the external app support below

// Toolbar keys: into the launcher's own key queue normally; into the virtual keyboard while a stock
// app runs (it reads the keyboard device itself). Esc also reaches the launcher's queue because the
// external-app runner derives its hold-to-exit timer from the launcher's Esc state.
void toolbar_emit(uint32_t code, int state)
{
    if (!external_running()) {
        cp0_keyboard_inject(code, state, 0);
        return;
    }
    if (state != KBD_KEY_REPEATED) native_vkbd::send(static_cast<unsigned short>(code), state == KBD_KEY_PRESSED ? 1 : 0);
    if (code == KEY_ESC) cp0_keyboard_inject(code, state, 0);
}

void toolbar_key_cb(lv_event_t *event)
{
    auto *key = static_cast<ToolbarKey *>(lv_event_get_user_data(event));
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        key->down = true;
        toolbar_emit(key->code, KBD_KEY_PRESSED);
        break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
        if (key->down) toolbar_emit(key->code, KBD_KEY_REPEATED);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_DELETE:
        if (key->down) {
            key->down = false;
            toolbar_emit(key->code, KBD_KEY_RELEASED);
        }
        break;
    default:
        break;
    }
}

void ensure_chrome()
{
    if (s_chrome) return;
    DefaultDisplayScope scope(cp0_display_native());

    const Layout &l = layout();
    const int screen_w = l.screen_w;
    // The layout service places the toolbar with the compat window (deck: y 340, 140 px; 480x320: y 220,
    // 100 px, the same 25 px margin above and below the window).
    const int bar_y = l.toolbar_y;
    const int bar_h = l.toolbar_h;

    s_chrome = make_black_screen();

    lv_obj_t *bar = lv_obj_create(s_chrome);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, screen_w, bar_h);
    lv_obj_set_pos(bar, 0, bar_y);
    lv_obj_set_style_pad_hor(bar, l.tb_pad_x, 0);
    lv_obj_set_style_pad_ver(bar, l.tb_pad_y, 0);
    lv_obj_set_style_pad_column(bar, l.tb_gap, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    constexpr int count = static_cast<int>(sizeof(s_keys) / sizeof(s_keys[0]));
    const int button_w = (screen_w - 2 * l.tb_pad_x - (count - 1) * l.tb_gap) / count;
    for (auto &key : s_keys) {
        lv_obj_t *button = lv_button_create(bar);
        lv_obj_set_size(button, button_w, bar_h - 2 * l.tb_pad_y);
        lv_obj_set_style_radius(button, l.tb_radius, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x2A2A2A), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(kAccent), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_add_event_cb(button, toolbar_key_cb, LV_EVENT_ALL, &key);

        lv_obj_t *label = lv_label_create(button);
        lv_label_set_text(label, key.text);
        lv_obj_set_style_text_font(label, key.symbol ? l.tb_symbol_font : l.tb_text_font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
    }
}

// ---------------------------------------------------------- external (stock) apps

constexpr const char *kVfbShim = "/usr/share/APPLaunch/lib/libapplaunch_vfb.so";

struct ExternalRun {
    int fd = -1;
    void *map = MAP_FAILED;
    size_t size = 0;
    std::vector<uint8_t> previous;
    bool ribbon = false; // the "hold Esc" hint is showing over the app's picture
    lv_timer_t *timer = nullptr;
    std::thread worker;
    std::atomic<bool> done{false};
    std::function<void()> on_exit;
};
ExternalRun *s_external = nullptr;

// gen= of the screen.state the launcher published (0 when there is none)
unsigned screen_state_gen()
{
    char path[512];
    if (cp0_ui_state_path(std::getenv("XDG_RUNTIME_DIR"), path, sizeof(path)) != 0) return 0;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char text[8192];
    size_t len = 0;
    while (len < sizeof(text)) {
        const ssize_t n = ::read(fd, text + len, sizeof(text) - len);
        if (n <= 0) break;
        len += static_cast<size_t>(n);
    }
    ::close(fd);
    return cp0_ui_state_parse_gen(text, len);
}

bool create_vfb_file(ExternalRun &run)
{
    const uint32_t width = APPLAUNCH_VFB_DEFAULT_WIDTH, height = APPLAUNCH_VFB_DEFAULT_HEIGHT;
    const uint32_t stride = width * 4;
    // room for two pages: some apps page-flip with FBIOPAN_DISPLAY
    run.size = APPLAUNCH_VFB_HEADER_BYTES + static_cast<size_t>(stride) * height * 2;
    run.fd = ::open(APPLAUNCH_VFB_DEFAULT_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (run.fd < 0) return false;
    (void)::fchmod(run.fd, 0666);
    if (::ftruncate(run.fd, static_cast<off_t>(run.size)) != 0) return false;
    run.map = ::mmap(nullptr, run.size, PROT_READ | PROT_WRITE, MAP_SHARED, run.fd, 0);
    if (run.map == MAP_FAILED) return false;
    std::memset(run.map, 0, run.size);
    auto *header = static_cast<applaunch_vfb_header_t *>(run.map);
    header->width = width;
    header->height = height;
    header->bpp = 32;
    header->stride = stride;
    header->yres_virtual = height;
    // VFB2: where the picture goes on the panel (the layout service's compat window), for the shim and later uses
    header->version = APPLAUNCH_VFB_VERSION;
    header->header_bytes = APPLAUNCH_VFB_HEADER_BYTES;
    header->canvas_mode = APPLAUNCH_VFB_CANVAS_NATIVE;
    if (const cp0_ui_metrics_t *m = cp0_ui_metrics_current()) {
        header->host_w = static_cast<uint32_t>(m->w);
        header->host_h = static_cast<uint32_t>(m->h);
        header->win_x = m->compat.x;
        header->win_y = m->compat.y;
        header->win_w = m->compat.w;
        header->win_h = m->compat.h;
        header->scale = static_cast<uint32_t>(m->compat.scale);
        header->ppmm_x100 = static_cast<uint32_t>(m->ppmm_x100);
    }
    header->gen = screen_state_gen();
    header->magic = APPLAUNCH_VFB_MAGIC; // last: the shim only attaches to an initialised file
    return true;
}

bool external_running()
{
    return s_external != nullptr;
}

void finish_external()
{
    ExternalRun *run = s_external;
    if (!run) return;
    s_external = nullptr;
    native_vkbd::stop_forwarding();
    cp0_display_set_key_sink(nullptr);
    cp0_display_set_touch_mode(CP0_DISPLAY_TOUCH_POINTER, 0, 0);
    if (run->timer) lv_timer_delete(run->timer);
    if (run->worker.joinable()) run->worker.join();
    if (run->ribbon) launcher_toast().hide();
    cp0_display_set_overlay_rect(0, 0, 0, 0);
    ui_global_hint::reset_external_esc_hint();
    if (run->map != MAP_FAILED) ::munmap(run->map, run->size);
    if (run->fd >= 0) ::close(run->fd);
    cp0_display_set_external(0);
    // An app built on an older cp0_lvgl may have described its 320x170 canvas in screen.state (report 023 F1):
    // the launcher's description of the panel comes back, gen + 1.
    (void)cp0_ui_state_republish();
    std::function<void()> on_exit = std::move(run->on_exit);
    delete run;
    if (on_exit) on_exit();
}

// UI thread, ~30 Hz: copy the app's picture into the compat window when it changed.
void external_poll(lv_timer_t *)
{
    ExternalRun *run = s_external;
    if (!run) return;
    // "Hold ESC 3s to return home": the runner raises a flag while Esc is held. The toast lives on the
    // compat display, which is otherwise silent while an app runs, so reserve its rectangle (compat
    // coordinates: 280x22 at the top centre) and keep the app's blit out of it while it shows.
    const bool want_ribbon = ui_global_hint::external_esc_hint_visible();
    if (want_ribbon != run->ribbon) {
        run->ribbon = want_ribbon;
        if (want_ribbon) {
            cp0_display_set_overlay_rect(CP0_UI_RIBBON_X, CP0_UI_RIBBON_Y, CP0_UI_RIBBON_W, CP0_UI_RIBBON_H);
            launcher_toast().show_persistent("Hold ESC 3s to return home");
        } else {
            launcher_toast().hide();
            cp0_display_set_overlay_rect(0, 0, 0, 0);
            run->previous.clear(); // force a full repaint of the app's picture over the ribbon area
        }
    }

    auto *header = static_cast<applaunch_vfb_header_t *>(run->map);
    if (header->attached && header->stride != 0) {
        const size_t bytes = static_cast<size_t>(header->stride) * header->height;
        const uint8_t *src = static_cast<const uint8_t *>(run->map) + APPLAUNCH_VFB_HEADER_BYTES +
                             static_cast<size_t>(header->yoffset) * header->stride;
        if (run->previous.size() != bytes || std::memcmp(run->previous.data(), src, bytes) != 0) {
            run->previous.assign(src, src + bytes);
            cp0_display_external_blit(run->previous.data(), static_cast<int>(header->width),
                                      static_cast<int>(header->height), static_cast<int>(header->stride));
        }
    }
    if (run->done.load()) finish_external();
}

} // namespace

namespace native_ui {

bool run_external(const std::string &command, bool keep_root, std::function<void()> on_exit)
{
    if (!enabled() || s_external) return false;
    auto *run = new ExternalRun();
    if (!create_vfb_file(*run)) {
        SLOGE("[EXT] cannot create the virtual framebuffer %s", APPLAUNCH_VFB_DEFAULT_PATH);
        if (run->map != MAP_FAILED) ::munmap(run->map, run->size);
        if (run->fd >= 0) ::close(run->fd);
        delete run;
        return false;
    }
    run->on_exit = std::move(on_exit);
    s_external = run;

    // compat chrome (toolbar) on the native display, compat display stops drawing
    begin_page(false, false);
    cp0_display_set_external(1);
    // Touch inside the app's picture: swipes = arrow keys, tap = Enter, delivered to the app through
    // the same virtual keyboard as the toolbar.
    const int swipe_choice = touch_settings::choice(s_launching_app);
    if (swipe_choice != touch_settings::kOff) {
        cp0_display_set_swipe_tap_key(swipe_choice == touch_settings::kSwipeFire ? 57 /* KEY_SPACE */ : 0);
        cp0_display_set_touch_mode(CP0_DISPLAY_TOUCH_SWIPE, 0, 0);
    } else {
        cp0_display_set_touch_mode(CP0_DISPLAY_TOUCH_POINTER, 0, 0); // touch does nothing inside the app
    }
    cp0_display_set_key_sink([](unsigned short code, int value) { native_vkbd::send(code, value); });

    // The app contract (cp0_app_contract.h): mode, window, canvas and state directory first, then the neutral
    // display of the virtual framebuffer.
    std::string wrapped = app_launch_env::export_for_current(app_launch_env::Mode::Compat) +
                          "export APPLAUNCH_VFB=" + APPLAUNCH_VFB_DEFAULT_PATH +
                          " LV_LINUX_FBDEV_DEVICE=/dev/fb0"
                          // The wrapped app draws into the 320x170 virtual framebuffer: drop the launcher's
                          // board.conf panel geometry (rotation, logical size, scale) so its own dpi-scaled
                          // backend draws upright and unscaled. APPLAUNCH_DISPLAY stays (status bar uses it).
                          " APPLAUNCH_FB=/dev/fb0 APPLAUNCH_ROTATE=0";
    // Feed the app one virtual keyboard that carries both the physical keys and the toolbar taps.
    if (native_vkbd::ensure()) {
        const char *physical = std::getenv("APPLAUNCH_LINUX_KEYBOARD_DEVICE");
        if (!physical || !physical[0]) physical = std::getenv("LV_LINUX_KEYBOARD_DEVICE");
        if (physical && physical[0]) {
            native_vkbd::start_forwarding(physical);
            wrapped += std::string(" LV_LINUX_KEYBOARD_DEVICE=") + native_vkbd::device_path();
        }
    }
    if (::access(kVfbShim, R_OK) == 0) wrapped += std::string(" LD_PRELOAD=") + kVfbShim;
    else SLOGW("[EXT] %s is missing: the app will draw to the real framebuffer", kVfbShim);
    wrapped += "; unset APPLAUNCH_LOGICAL APPLAUNCH_COMPAT_SCALE APPLAUNCH_PANEL_MM; " + command;
    SLOGI("[EXT] run: %s", wrapped.c_str());

    run->timer = lv_timer_create(external_poll, 33, nullptr);
    run->worker = std::thread([run, wrapped, keep_root] {
        const int rc = cp0_process_exec_blocking(wrapped.c_str(), keep_root ? 1 : 0);
        SLOGI("[EXT] app exited rc=%d", rc);
        run->done.store(true);
        cp0_lvgl_wake(); // let the UI loop notice right away
    });
    return true;
}

void fill_status(cp0_statusbar_state_t &state)
{
    cp0_wifi_status_t wifi{};
    const bool connected = cp0_wifi_status_read(&wifi) == 0 && wifi.connected;
    cp0_ui_status_set_wifi(&state, connected ? 1 : 0, connected ? wifi.signal : 0);

    // The callbacks may complete on another thread, so they only touch process-wide atomics.
    static std::atomic<bool> bt_powered{false};
    static std::atomic<bool> bt_connected{false};
    cp0_signal_bt_api({"BtStatus"}, [](int code, std::string data) {
        bt_powered = code == 0 && !data.empty() && data[0] == '1';
    });
    cp0_signal_bt_api({"BtConnectedList"}, [](int code, std::string data) {
        // List commands report the device count as their code (negative on error).
        bt_connected = code >= 0 && data.find(':') != std::string::npos; // any listed device
    });
    state.bt_on = bt_powered.load();
    state.bt_connected = bt_connected.load();

    // The SSH indicator (2026-10-30): the status feed's last sample (launcher_ssh.hpp), the same values the apps read in
    // status.state. Nothing while Settings > System > SSH indicator is Off; steady while the lock screen is up (no blink,
    // so no redraw, behind it).
    const ssh_indicator::Snapshot ssh =
        ssh_indicator::shown(launcher_ssh::snapshot(), launcher_ssh::enabled(), ui_screensaver_is_active() != 0);
    cp0_ui_status_set_ssh(&state, ssh.count, ssh.active);
}

bool enabled()
{
    return cp0_display_available() != 0;
}

void attach(Launch *launch)
{
    cancel_move_mode(); // a tile carried over another app list's grid goes back first
    s_launch = launch;
}

void set_launching_app(const std::string &name)
{
    s_launching_app = name;
}

void refresh_apps()
{
    if (!enabled() || !s_launch) return;
    ensure_home();
    DefaultDisplayScope scope(cp0_display_native());

    // study 046 P2: the tiles go; a hold in progress ends, a carried tile is dropped where it is (saved if it moved)
    hold_stop();
    move_drop(false);
    lv_obj_clean(s_grid);
    s_tiles.clear();
    s_dots.clear();
    const std::size_t count = s_launch->app_count();
    for (std::size_t i = 0; i < count; ++i)
        if (const app *item = s_launch->app_at(i)) build_tile(static_cast<int>(i), *item);
    select_tile(s_selected < static_cast<int>(s_tiles.size()) ? s_selected : 0, false);
    if (!s_resident_keys.empty()) apply_resident_dots();
}

void set_resident_apps(const std::vector<std::string> &exec_keys)
{
    if (!enabled() || exec_keys == s_resident_keys) return;
    s_resident_keys = exec_keys;
    apply_resident_dots();
    if (s_move.badge) lv_obj_move_foreground(s_move.badge); // a dot made meanwhile stays under the move badge
}

void cancel_move_mode()
{
    if (!enabled()) return;
    hold_stop();
    s_swallow_click = false;
    move_cancel();
}

void finish_move_mode()
{
    if (!enabled()) return;
    hold_stop();
    move_drop(false);
}

std::string app_icon(const std::string &exec_key)
{
    if (!s_launch || exec_key.empty()) return std::string();
    const std::size_t count = s_launch->app_count();
    for (std::size_t i = 0; i < count; ++i)
        if (const app *item = s_launch->app_at(i))
            if (item->Exec == exec_key) return item->Icon;
    return std::string();
}

bool external_app_running()
{
    return external_running();
}

void show_home()
{
    if (!enabled()) return;
    static bool mirror_started = false;   // the Store's shared source list follows the backend, once per launch
    if (!mirror_started) {
        mirror_started = true;
        settings_apps::Jobs::mirror_at_start();
    }
    // the fallback on-screen keyboard belongs to the page that asked for it: it goes with the page (task 014 P2b)
    launcher_osk::install();
    launcher_osk::dismiss();
    // Settings > System > Pointer navigation, applied once (the launcher's pointer reader and input.state)
    launcher_pointer::install();
    // Settings > System > SSH indicator, once: the status feed's thread starts sampling when it is On
    launcher_ssh::install();
    ensure_home();

    // Park an empty screen on the compat display so a finishing app's screen is
    // no longer active (it is deleted right after we return).
    if (!s_idle) {
        DefaultDisplayScope scope(cp0_display_compat());
        s_idle = make_black_screen();
    }
    lv_screen_load(s_idle);

    cp0_display_set_mode(CP0_DISPLAY_MODE_NATIVE);
    lv_screen_load(s_home);
    lv_obj_invalidate(s_home);
    if (s_home_strip) s_home_strip->refresh(); // icons are current as soon as the grid is visible
}

void begin_page(bool native_layout, bool touch_list, bool touch_swipe, unsigned short swipe_tap_key)
{
    if (!enabled()) return;
    // A new page: the on-screen keyboard of the previous one goes (an Esc it still owes is released first, so the
    // stuck-Esc check below sees the final state). Native pages show it again for their own fields (task 014 P2b).
    launcher_osk::install();
    launcher_osk::dismiss();
    // An on-screen Esc whose button was destroyed mid-press never reports its release; clear the
    // stuck state before the Esc watchdog is armed for this page (it would restart the launcher).
    if (cp0_esc_state_read() != 0) cp0_keyboard_inject(KEY_ESC, KBD_KEY_RELEASED, 0);
    // Settings-style pages only understand keys: turn touch gestures into them (row = 21 px,
    // highlighted row at y = 96 in the 320x170 compat coordinates).
    // Games: a swipe is the arrow key for its direction, a tap is Enter.
    // Games: a swipe is the arrow key for its direction, a tap is Enter (or Space to fire). The user's
    // choice in Settings > Touch wins over the page's built-in default.
    int swipe_choice = touch_swipe ? (swipe_tap_key != 0 ? touch_settings::kSwipeFire : touch_settings::kSwipe)
                                   : touch_settings::kOff;
    if (!touch_list && !s_launching_app.empty()) swipe_choice = touch_settings::choice(s_launching_app);
    cp0_display_set_swipe_tap_key(swipe_choice == touch_settings::kSwipeFire ? 57 /* KEY_SPACE */ : 0);
    cp0_display_set_touch_mode(touch_list ? CP0_DISPLAY_TOUCH_LIST
                               : swipe_choice != touch_settings::kOff ? CP0_DISPLAY_TOUCH_SWIPE
                                                                      : CP0_DISPLAY_TOUCH_POINTER,
                               96, 21);
    if (native_layout) {
        // Native pages create their screen on the default display: make it the native one.
        cp0_display_set_mode(CP0_DISPLAY_MODE_NATIVE);
        return;
    }
    enter_compat();
}

void enter_compat()
{
    if (!enabled()) return;
    ensure_chrome();
    cp0_display_set_mode(CP0_DISPLAY_MODE_COMPAT);
    lv_screen_load(s_chrome);
    lv_obj_invalidate(s_chrome);
}

} // namespace native_ui

#else // SDL simulator and non-Linux builds: native UI is not available

namespace native_ui {

bool enabled() { return false; }
void fill_status(cp0_statusbar_state_t &) {}
void attach(Launch *) {}
void set_launching_app(const std::string &) {}
void show_home() {}
void enter_compat() {}
void begin_page(bool, bool, bool, unsigned short) {}
bool run_external(const std::string &, bool, std::function<void()>) { return false; }
void refresh_apps() {}
void set_resident_apps(const std::vector<std::string> &) {}
void cancel_move_mode() {}
void finish_move_mode() {}
std::string app_icon(const std::string &) { return std::string(); }
bool external_app_running() { return false; }

} // namespace native_ui

#endif
