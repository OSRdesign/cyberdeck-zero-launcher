/*
 * Native full-screen UI for the Raspberry Pi Zero 2W + Waveshare 2.8" DPI LCD port.
 */

#include "native_ui.hpp"

#if defined(__linux__) && !defined(HAL_PLATFORM_SDL)

#include "app_launch_env.hpp"
#include "launch.h"
#include "launcher_osk.hpp"
#include "launcher_pointer.hpp"
#include "launcher_toast.h"
#include "native_vkbd.hpp"
#include "settings/settings_apps_jobs.hpp"
#include "touch_settings.hpp"
#include "ui_global_hint.h"

#include "cp0_display.h"
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

void tile_clicked_cb(lv_event_t *event)
{
    launch_tile(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

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
}

void build_tile(int index, const app &item)
{
    const Layout &l = layout();
    const int width = l.tile_w;

    lv_obj_t *tile = lv_obj_create(s_grid);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
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
    lv_obj_add_event_cb(tile, tile_clicked_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(index)));

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
}

bool enabled()
{
    return cp0_display_available() != 0;
}

void attach(Launch *launch)
{
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

    lv_obj_clean(s_grid);
    s_tiles.clear();
    const std::size_t count = s_launch->app_count();
    for (std::size_t i = 0; i < count; ++i)
        if (const app *item = s_launch->app_at(i)) build_tile(static_cast<int>(i), *item);
    select_tile(s_selected < static_cast<int>(s_tiles.size()) ? s_selected : 0, false);
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

} // namespace native_ui

#endif
