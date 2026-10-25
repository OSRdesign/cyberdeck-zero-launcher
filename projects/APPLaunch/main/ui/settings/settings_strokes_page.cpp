/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Strokes training (see settings_strokes_page.hpp; report 042).
 */

#include "settings_strokes_page.hpp"

#include "../launcher_zone.hpp"

#include "cp0_font_service.hpp"
#include "cp0_ui_list.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_widgets.hpp"
#include "cp0_zone_ui.h"
#include "strokes_train.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>

std::unique_ptr<DComponens::LvglComponensBase> settings_strokes_training_factory(lv_obj_t *, const NodeIter &,
                                                                                 std::function<void()>)
{
    return nullptr; /* native only: settings_native_panel_create() */
}

namespace {

constexpr const char *kFailing = "b p s e m o q r y z"; /* refused at the owner's first board test (report 042) */
constexpr const char *kLogName = "strokes-training.txt";
constexpr const char *kTextFace = "Montserrat-Medium.ttf"; /* glyph labels beyond ASCII (c cedilla, ae) */
constexpr uint32_t kButton = 0x2A2A2A;          /* the Dialog's buttons */
constexpr uint32_t kButtonPressed = 0x2D9CDB;
constexpr uint32_t kFocus = 0x2D9CDB;           /* the TextField's editing border: the focused button */
constexpr uint32_t kText = CP0_UI_LIST_TEXT_RGB;
constexpr uint32_t kGrey = CP0_UI_LIST_SECTION_RGB;
constexpr uint32_t kGuideInk = 0x6E7878;        /* the glyph's shape: light ink */
constexpr uint32_t kBoxBg = 0x141818;           /* the writing boxes' (cp0_zone.h) */
constexpr uint32_t kBoxBorder = 0x465050;
constexpr uint32_t kFlashMs = 600;
constexpr int kMaxLinePts = 160;                /* points of a drawn shape (thinned beyond) */

uint32_t verdict_rgb(int verdict)
{
    switch (verdict) {
    case STROKES_TRAIN_OK: return CP0_UI_LIST_OK_RGB;
    case STROKES_TRAIN_WRONG: return CP0_UI_LIST_WARN_RGB;
    case STROKES_TRAIN_REFUSED: return CP0_UI_LIST_BAD_RGB;
    default: return kGrey;
    }
}

std::string now_text(bool iso)
{
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[40];
    std::strftime(buf, sizeof(buf), iso ? "%Y-%m-%dT%H:%M:%S" : "%Y-%m-%d %H:%M", &tm);
    return buf;
}

std::string dir_of(const std::string &path)
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

void make_dirs(const std::string &dir)
{
    for (std::size_t at = 1; at <= dir.size(); ++at)
        if (at == dir.size() || dir[at] == '/') (void)mkdir(dir.substr(0, at).c_str(), 0755);
}

bool read_file(const std::string &path, std::string &out)
{
    out.clear();
    FILE *f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

/* written whole or not at all: a temporary file renamed over the old one */
bool write_file(const std::string &path, const std::string &text)
{
    make_dirs(dir_of(path));
    const std::string tmp = path + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    if (lv_obj_get_group(o)) lv_group_remove_obj(o);
    return o;
}

lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t rgb)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");
    if (font) lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
    lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

/* A drawn shape: points scaled uniformly into a box side x side with a margin, centred; storage kept for lv_line. */
struct Shape {
    lv_obj_t *line = nullptr;
    lv_obj_t *dot = nullptr; /* where it starts (the guide only) */
    std::array<lv_point_precise_t, kMaxLinePts> pts{};

    void set(const float *xy, int n, int side, int margin)
    {
        if (!line) return;
        int count = 0;
        if (n >= 2) {
            float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
            for (int i = 1; i < n; ++i) {
                minx = std::min(minx, xy[2 * i]);
                maxx = std::max(maxx, xy[2 * i]);
                miny = std::min(miny, xy[2 * i + 1]);
                maxy = std::max(maxy, xy[2 * i + 1]);
            }
            const float span = std::max(1.0f, std::max(maxx - minx, maxy - miny));
            const float k = static_cast<float>(side - 2 * margin) / span;
            const float ox = (static_cast<float>(side) - (maxx - minx) * k) / 2.0f;
            const float oy = (static_cast<float>(side) - (maxy - miny) * k) / 2.0f;
            const int step = (n + kMaxLinePts - 1) / kMaxLinePts;
            for (int i = 0; i < n && count < kMaxLinePts; i += step) {
                pts[count].x = static_cast<lv_value_precise_t>(ox + (xy[2 * i] - minx) * k);
                pts[count].y = static_cast<lv_value_precise_t>(oy + (xy[2 * i + 1] - miny) * k);
                count++;
            }
            if (step > 1 && count > 0) {
                pts[count - 1].x = static_cast<lv_value_precise_t>(ox + (xy[2 * n - 2] - minx) * k);
                pts[count - 1].y = static_cast<lv_value_precise_t>(oy + (xy[2 * n - 1] - miny) * k);
            }
        }
        lv_line_set_points(line, pts.data(), static_cast<uint32_t>(count));
        if (dot) {
            if (count > 0) {
                const int r = lv_obj_get_width(dot) / 2;
                lv_obj_set_pos(dot, static_cast<int>(pts[0].x) - r, static_cast<int>(pts[0].y) - r);
                lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
};

class StrokesTrainingPanel : public SettingsNativePanel {
public:
    explicit StrokesTrainingPanel(const SettingsNativePanelContext &context)
        : page_(context.page), group_(context.group), back_(context.back)
    {
        overlay_path_ = cp0_zone_ui_overlay_path() ? cp0_zone_ui_overlay_path() : "";
        if (!overlay_path_.empty()) log_path_ = dir_of(overlay_path_) + "/" + kLogName;
        std::string overlay;
        (void)read_file(overlay_path_, overlay);
        session_ = strokes_train_new(cp0_zone_ui_table(), cp0_zone_ui_config(), kFailing, overlay.c_str());
        /* the band shows on this page whatever the fields say, and its strokes come here (nothing is typed) */
        launcher_zone::set_pinned(true);
        cp0_zone_ui_set_capture(&StrokesTrainingPanel::capture_cb, this);
        build();
        refresh();
    }

    ~StrokesTrainingPanel() override
    {
        cp0_zone_ui_set_capture(nullptr, nullptr);
        launcher_zone::set_pinned(false);
        lv_async_call_cancel(run_posted_cb, this);
        if (flash_) lv_anim_delete(flash_, nullptr);
        dialog_.reset();
        page_.clear_body();
        strokes_train_free(session_);
    }

    bool back() override
    {
        if (!dialog_) return false;
        close_dialog();
        return true;
    }

private:
    /* ---------------------------------------------------------------- the strokes */

    static void capture_cb(const cp0_zone_capture_t *capture, void *user)
    {
        auto *self = static_cast<StrokesTrainingPanel *>(user);
        if (self && capture) self->on_capture(*capture);
    }

    void on_capture(const cp0_zone_capture_t &c)
    {
        if (!session_ || dialog_ || c.n <= 0) return;
        const int phase = strokes_train_phase(session_);
        const int item = strokes_train_current(session_);
        strokes_train_set_time(session_, now_text(false).c_str());
        const unsigned ev = strokes_train_stroke(session_, c.pts, c.n);
        if (!(ev & STROKES_TRAIN_EV_ATTEMPT)) return;
        log_attempt(c);
        if (phase == STROKES_TRAIN_TRAINING && item >= 0 && attempts_ < static_cast<int>(thumbs_.size())) {
            std::vector<float> xy(static_cast<std::size_t>(2 * c.n));
            for (int i = 0; i < c.n; ++i) {
                xy[2 * static_cast<std::size_t>(i)] = c.pts[i].x;
                xy[2 * static_cast<std::size_t>(i) + 1] = c.pts[i].y;
            }
            kept_[static_cast<std::size_t>(attempts_)] = std::move(xy);
            attempts_++;
        }
        const strokes_train_attempt *a = strokes_train_last(session_);
        std::string line = a ? verdict_text(*a) : std::string();
        if ((ev & STROKES_TRAIN_EV_OVERLAY) && phase == STROKES_TRAIN_TRAINING && item >= 0) {
            strokes_train_stats st;
            strokes_train_item_stats(session_, item, &st);
            char done[160];
            std::snprintf(done, sizeof(done), "  -  %s done: %d of %d recognised, %d of your strokes kept",
                          label_of(item).c_str(), st.ok_before, st.tried, st.added);
            line += done;
        }
        apply(ev);
        flash(line, a ? a->verdict : STROKES_TRAIN_NONE);
    }

    /* the capture log: the board's verdict as a comment line, then the stroke as the panel reported it */
    void log_attempt(const cp0_zone_capture_t &c)
    {
        if (log_path_.empty()) return;
        const int item = strokes_train_last_item(session_);
        const strokes_train_item *it = strokes_train_item_at(session_, item);
        char comment[512];
        if (!it || strokes_train_log_comment(session_, now_text(true).c_str(), comment, sizeof(comment)) < 0) return;
        make_dirs(dir_of(log_path_));
        FILE *f = std::fopen(log_path_.c_str(), "ae");
        if (!f) {
            std::fprintf(stderr, "[strokes-training] %s: cannot append: %s\n", log_path_.c_str(), std::strerror(errno));
            return;
        }
        std::fseek(f, 0, SEEK_END);
        if (std::ftell(f) == 0)
            std::fprintf(f, "# strokes-corpus 1\n# written by Settings > System > Strokes training (report 042); "
                            "format: tools/strokes/README.md \"Corpus format\"\n");
        std::fprintf(f, "%s\n%s\t%s\t%.3f\t%s\n", comment, it->id, c.box == STROKES_BOX_DIGITS ? "digits" : "letters",
                     static_cast<double>(c.ppmm), c.corpus ? c.corpus : "");
        std::fclose(f);
    }

    /* what a call changed: the overlay saved and the tables loaded again, the page drawn again */
    void apply(unsigned ev)
    {
        if (ev & STROKES_TRAIN_EV_OVERLAY) save_and_reload();
        if (ev & (STROKES_TRAIN_EV_MOVED | STROKES_TRAIN_EV_PHASE)) {
            attempts_ = 0;
            for (auto &k : kept_) k.clear();
        }
        refresh();
    }

    void save_and_reload()
    {
        error_.clear();
        if (overlay_path_.empty()) {
            error_ = "No user overlay file: your strokes are not kept";
            return;
        }
        if (!write_file(overlay_path_, strokes_train_overlay(session_))) {
            error_ = "Cannot write " + overlay_path_;
            return;
        }
        strokes_error err;
        if (cp0_zone_ui_reload(&err) != 0) error_ = std::string("The overlay was refused: ") + err.msg;
        strokes_train_set_table(session_, cp0_zone_ui_table());
        std::fprintf(stderr, "[strokes-training] %s saved, tables reloaded%s%s\n", overlay_path_.c_str(),
                     error_.empty() ? "" : ": ", error_.c_str());
    }

    /* ---------------------------------------------------------------- the page */

    void build()
    {
        const cp0_ui_roller_page_t &l = page_.layout();
        const cp0_ui_metrics_t *m = cp0_ui_metrics_get();
        const int target = m ? m->tok[CP0_TOK_TARGET] : l.row_h;
        const int width = l.roller_w - 2 * l.inset;
        const int big_px = cp0_ui_font_snap(std::max(l.text_px + 6, l.title_px));
        const int small_px = l.caption_px;
        const lv_font_t *big = cp0_fonts().get(kTextFace, static_cast<uint16_t>(big_px));
        const lv_font_t *body = cp0_fonts().get(kTextFace, static_cast<uint16_t>(l.value_px));
        const lv_font_t *small = cp0_fonts().get(kTextFace, static_cast<uint16_t>(small_px));
        lv_obj_t *column = page_.body();
        /* everything fits above the band: the column never scrolls (the summary's rows scroll in their own box) */
        lv_obj_remove_flag(column, LV_OBJ_FLAG_SCROLLABLE);

        prompt_ = text(column, big ? big : cp0_ui_font_px(big_px), kText);
        lv_obj_set_width(prompt_, width);
        lv_label_set_long_mode(prompt_, LV_LABEL_LONG_MODE_DOTS);
        hint_ = text(column, small ? small : cp0_ui_font_px(small_px), kGrey);
        lv_obj_set_width(hint_, width);
        lv_label_set_long_mode(hint_, LV_LABEL_LONG_MODE_WRAP);

        /* the guide and the attempts: a square each side, as high as the body leaves */
        const int line_h = lv_font_get_line_height(lv_obj_get_style_text_font(prompt_, LV_PART_MAIN)) +
                           2 * lv_font_get_line_height(lv_obj_get_style_text_font(hint_, LV_PART_MAIN));
        const int result_h = lv_font_get_line_height(body ? body : cp0_ui_font_px(l.value_px)) + l.gap;
        const int room = l.body_h - cp0::ui::osk::bottom_inset() - 2 * l.gap - 4 * l.gap - line_h - result_h - target;
        side_ = std::max(72, std::min({room - l.gap, (width - l.gap) / 2, 200}));
        stage_ = plain(column);
        lv_obj_set_size(stage_, width, side_);
        guide_box_ = box(stage_, 0, 0, side_);
        guide_.line = lv_line_create(guide_box_);
        style_line(guide_.line, kGuideInk, std::max(3, side_ / 30));
        guide_.dot = plain(guide_box_);
        const int dot = std::max(8, side_ / 14);
        lv_obj_set_size(guide_.dot, dot, dot);
        lv_obj_set_style_radius(guide_.dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(guide_.dot, lv_color_hex(CP0_UI_LIST_OK_RGB), 0);
        lv_obj_set_style_bg_opa(guide_.dot, LV_OPA_COVER, 0);
        const int thumb = (side_ - l.gap) / 2;
        for (std::size_t k = 0; k < thumbs_.size(); ++k) {
            const int x = side_ + l.gap + static_cast<int>(k % 2) * (thumb + l.gap);
            const int y = static_cast<int>(k / 2) * (thumb + l.gap);
            thumbs_[k].box = box(stage_, x, y, thumb);
            thumbs_[k].shape.line = lv_line_create(thumbs_[k].box);
            style_line(thumbs_[k].shape.line, kText, std::max(2, thumb / 30));
            thumbs_[k].label = text(thumbs_[k].box, small ? small : cp0_ui_font_px(small_px), kGrey);
            lv_obj_align(thumbs_[k].label, LV_ALIGN_TOP_LEFT, 4, 2);
        }
        thumb_side_ = thumb;

        summary_ = plain(column);
        lv_obj_set_width(summary_, width);
        lv_obj_set_height(summary_, side_);
        lv_obj_add_flag(summary_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(summary_, LV_DIR_VER);
        lv_obj_set_flex_flow(summary_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(summary_, l.gap / 2, 0);
        lv_obj_add_flag(summary_, LV_OBJ_FLAG_HIDDEN);
        summary_font_ = body ? body : cp0_ui_font_px(l.value_px);

        result_ = text(column, body ? body : cp0_ui_font_px(l.value_px), kText);
        lv_obj_set_size(result_, width, result_h);
        lv_obj_set_style_pad_left(result_, l.gap / 2, 0);
        lv_obj_set_style_pad_top(result_, l.gap / 2, 0);
        lv_obj_set_style_radius(result_, l.esc_radius, 0);
        lv_label_set_long_mode(result_, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_t *row = plain(column);
        lv_obj_set_size(row, width, target);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, l.gap / 2, 0);
        const lv_font_t *button_font = small ? small : cp0_ui_font_px(small_px);
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            lv_obj_t *b = lv_button_create(row);
            lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            if (lv_obj_get_group(b)) lv_group_remove_obj(b);
            lv_obj_set_height(b, target);
            lv_obj_set_flex_grow(b, 1);
            lv_obj_set_style_radius(b, l.esc_radius, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(kButton), 0);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(kButtonPressed), LV_STATE_PRESSED);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_border_width(b, 0, 0);
            lv_obj_set_style_pad_hor(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(kFocus), LV_STATE_FOCUS_KEY);
            lv_obj_set_style_border_width(b, 2, LV_STATE_FOCUS_KEY);
            lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUS_KEY);
            lv_obj_set_user_data(b, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
            lv_obj_add_event_cb(b, button_cb, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(b, button_cb, LV_EVENT_KEY, this);
            button_label_[i] = text(b, button_font, kText);
            lv_obj_center(button_label_[i]);
            buttons_[i] = b;
            if (group_) lv_group_add_obj(group_, b);
        }
        if (group_ && buttons_[0]) lv_group_focus_obj(buttons_[0]);
    }

    lv_obj_t *box(lv_obj_t *parent, int x, int y, int side)
    {
        lv_obj_t *b = plain(parent);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_size(b, side, side);
        lv_obj_set_style_bg_color(b, lv_color_hex(kBoxBg), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(kBoxBorder), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_radius(b, page_.layout().esc_radius, 0);
        return b;
    }

    static void style_line(lv_obj_t *line, uint32_t rgb, int width)
    {
        lv_obj_remove_style_all(line);
        lv_obj_set_pos(line, 0, 0);
        lv_obj_set_size(line, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_line_width(line, width, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(rgb), 0);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }

    /* ---------------------------------------------------------------- what the page shows now */

    enum Button { kBack = 0, kSkip, kRedo, kReset, kFinish, kButtons };

    std::string label_of(int item) const
    {
        const strokes_train_item *it = strokes_train_item_at(session_, item);
        return it ? it->label : std::string("?");
    }

    /* a glyph id the recogniser named: its label when the session knows it ("comma" -> ",") */
    std::string label_of_id(const char *id) const
    {
        for (int i = 0; id && i < strokes_train_count(session_); ++i)
            if (std::strcmp(strokes_train_item_at(session_, i)->id, id) == 0) return label_of(i);
        return id && id[0] ? id : "?";
    }

    std::string verdict_text(const strokes_train_attempt &a) const
    {
        switch (a.verdict) {
        case STROKES_TRAIN_OK: return "Recognised as " + label_of_id(a.got);
        case STROKES_TRAIN_WRONG: return "Read as " + label_of_id(a.got);
        case STROKES_TRAIN_REFUSED:
            return a.got[0] ? "Refused (closest: " + label_of_id(a.got) + ")" : std::string("Refused");
        default: return std::string();
        }
    }

    static const char *group_text(int group)
    {
        switch (group) {
        case STROKES_TRAIN_FAILING: return "The letters refused on the board first";
        case STROKES_TRAIN_LETTERS: return "Letters";
        case STROKES_TRAIN_DIGITS: return "Digits";
        default: return "Gestures and punctuation (optional)";
        }
    }

    void set_button(Button b, const char *label, bool shown)
    {
        lv_obj_t *obj = buttons_[static_cast<std::size_t>(b)];
        if (!obj) return;
        lv_label_set_text(button_label_[static_cast<std::size_t>(b)], label);
        if (shown) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    void refresh()
    {
        if (!session_) {
            lv_label_set_text(prompt_, "Strokes training");
            lv_label_set_text(hint_, "The recogniser's tables are not loaded");
            return;
        }
        const int phase = strokes_train_phase(session_);
        const int cur = strokes_train_current(session_);
        const strokes_train_item *it = strokes_train_item_at(session_, cur);
        char buf[200];
        if (phase == STROKES_TRAIN_DONE || !it) {
            show_summary();
            return;
        }
        lv_obj_add_flag(summary_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(stage_, LV_OBJ_FLAG_HIDDEN);
        if (phase == STROKES_TRAIN_TRAINING) {
            std::snprintf(buf, sizeof(buf), "Draw %s  (%d/%d)", it->label, strokes_train_progress(session_) + 1,
                          STROKES_TRAIN_REPS);
            lv_label_set_text(prompt_, buf);
            const bool digits = it->set == STROKES_SET_DIGITS;
            std::string hint = std::string(group_text(it->group)) + " - write it in the " +
                               (digits ? "digits" : "letters") + " box";
            if (it->set == STROKES_SET_PUNCT) hint += ", without the tap before it";
            if (it->set == STROKES_SET_ANY) hint += " (a gesture)";
            std::snprintf(buf, sizeof(buf), "  -  glyph %d of %d", cur + 1, strokes_train_count(session_));
            hint += buf;
            lv_label_set_text(hint_, (error_.empty() ? hint : error_).c_str());
        } else {
            std::snprintf(buf, sizeof(buf), "Check %s  (%d/%d)", it->label, strokes_train_progress(session_) + 1,
                          strokes_train_retest_count(session_));
            lv_label_set_text(prompt_, buf);
            lv_label_set_text(hint_, error_.empty() ? "Once more each glyph you trained, with your new templates"
                                                    : error_.c_str());
        }
        lv_obj_set_style_text_color(hint_, lv_color_hex(error_.empty() ? kGrey : CP0_UI_LIST_BAD_RGB), 0);

        /* the guide: the glyph's first variant (the chart's stroke), a dot where it starts */
        std::array<float, 2 * 256> xy{};
        const int n = strokes_table_template_points(cp0_zone_ui_table(), it->id, it->set, 0, xy.data(), 256);
        guide_.set(xy.data(), std::min(n, 256), side_, side_ / 8);

        /* the attempts of this glyph */
        const bool training = phase == STROKES_TRAIN_TRAINING;
        for (std::size_t k = 0; k < thumbs_.size(); ++k) {
            Thumb &t = thumbs_[k];
            const strokes_train_attempt *a =
                training ? strokes_train_attempt_at(session_, static_cast<int>(k)) : nullptr;
            const bool have = a && k < static_cast<std::size_t>(attempts_) && !kept_[k].empty();
            if (have) t.shape.set(kept_[k].data(), static_cast<int>(kept_[k].size() / 2), thumb_side_, thumb_side_ / 8);
            else t.shape.set(nullptr, 0, thumb_side_, 0);
            lv_obj_set_style_border_color(t.box, lv_color_hex(have ? verdict_rgb(a->verdict) : kBoxBorder), 0);
            lv_obj_set_style_border_width(t.box, have ? 2 : 1, 0);
            const std::string mark = !have ? std::string()
                                     : a->verdict == STROKES_TRAIN_REFUSED ? "~" + label_of_id(a->got)
                                                                           : label_of_id(a->got);
            lv_label_set_text(t.label, mark.c_str());
            lv_obj_set_style_text_color(t.label, lv_color_hex(have ? verdict_rgb(a->verdict) : kGrey), 0);
            if (training) lv_obj_remove_flag(t.box, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(t.box, LV_OBJ_FLAG_HIDDEN);
        }
        set_button(kBack, "Back", training);
        set_button(kSkip, "Skip", true);
        set_button(kRedo, "Redo", training);
        set_button(kReset, "Reset all", training);
        set_button(kFinish, training ? "Finish" : "Summary", true);
        keep_focus_visible();
    }

    void show_summary()
    {
        lv_obj_add_flag(stage_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(summary_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clean(summary_);
        strokes_train_stats tot;
        strokes_train_totals(session_, &tot);
        lv_label_set_text(prompt_, "Summary");
        char buf[200];
        if (tot.trained == 0) {
            lv_label_set_text(hint_, "No glyph was trained in this session");
        } else {
            std::snprintf(buf, sizeof(buf), "Before: %d of %d recognised (%d %%)  -  after: %d of %d (%d %%)",
                          tot.ok_before, tot.tried, tot.tried ? 100 * tot.ok_before / tot.tried : 0, tot.ok_after,
                          tot.retested, tot.retested ? 100 * tot.ok_after / tot.retested : 0);
            lv_label_set_text(hint_, buf);
        }
        lv_obj_set_style_text_color(hint_, lv_color_hex(kText), 0);
        const cp0_ui_roller_page_t &l = page_.layout();
        const int width = l.roller_w - 2 * l.inset;
        for (int i = 0; i < strokes_train_count(session_); ++i) {
            strokes_train_stats st;
            strokes_train_item_stats(session_, i, &st);
            if (!st.trained) continue;
            lv_obj_t *row = plain(summary_);
            lv_obj_set_size(row, width, LV_SIZE_CONTENT);
            lv_obj_t *glyph = text(row, summary_font_, kText);
            lv_label_set_text(glyph, label_of(i).c_str());
            lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_t *before = text(row, summary_font_,
                                    st.ok_before == st.tried ? CP0_UI_LIST_OK_RGB
                                    : st.ok_before == 0      ? CP0_UI_LIST_BAD_RGB
                                                             : CP0_UI_LIST_WARN_RGB);
            std::snprintf(buf, sizeof(buf), "before %d/%d", st.ok_before, st.tried);
            lv_label_set_text(before, buf);
            lv_obj_align(before, LV_ALIGN_LEFT_MID, width / 4, 0);
            lv_obj_t *after = text(row, summary_font_,
                                   st.retested == 0              ? kGrey
                                   : st.ok_after == st.retested ? CP0_UI_LIST_OK_RGB
                                                                : CP0_UI_LIST_BAD_RGB);
            if (st.retested) std::snprintf(buf, sizeof(buf), "after %d/%d", st.ok_after, st.retested);
            else std::snprintf(buf, sizeof(buf), "after -");
            lv_label_set_text(after, buf);
            lv_obj_align(after, LV_ALIGN_LEFT_MID, width * 3 / 5, 0);
            std::snprintf(buf, sizeof(buf), "+%d", st.added);
            lv_obj_t *added = text(row, summary_font_, kGrey);
            lv_label_set_text(added, buf);
            lv_obj_align(added, LV_ALIGN_RIGHT_MID, 0, 0);
        }
        set_button(kBack, "Back", false);
        set_button(kSkip, "Skip", false);
        set_button(kRedo, "Redo", false);
        set_button(kReset, "Reset all", false);
        set_button(kFinish, "Train again", true);
        keep_focus_visible();
    }

    void keep_focus_visible()
    {
        if (!group_) return;
        lv_obj_t *focused = lv_group_get_focused(group_);
        if (focused && !lv_obj_has_flag(focused, LV_OBJ_FLAG_HIDDEN)) return;
        for (lv_obj_t *b : buttons_)
            if (b && !lv_obj_has_flag(b, LV_OBJ_FLAG_HIDDEN)) {
                lv_group_focus_obj(b);
                return;
            }
    }

    /* the result line: its text in the verdict's colour, a short flash of that colour behind it */
    void flash(const std::string &line, int verdict)
    {
        const uint32_t rgb = verdict_rgb(verdict);
        lv_label_set_text(result_, line.c_str());
        lv_obj_set_style_text_color(result_, lv_color_hex(rgb), 0);
        lv_obj_set_style_bg_color(result_, lv_color_hex(rgb), 0);
        if (line.empty()) {
            lv_obj_set_style_bg_opa(result_, LV_OPA_TRANSP, 0);
            return;
        }
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, result_);
        lv_anim_set_values(&a, LV_OPA_50, LV_OPA_TRANSP);
        lv_anim_set_duration(&a, kFlashMs);
        lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
            lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), static_cast<lv_opa_t>(v), 0);
        });
        lv_anim_start(&a);
        flash_ = result_;
    }

    /* ---------------------------------------------------------------- the buttons */

    static void button_cb(lv_event_t *event)
    {
        auto *self = static_cast<StrokesTrainingPanel *>(lv_event_get_user_data(event));
        auto *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
        if (!self || !obj) return;
        const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        if (lv_event_get_code(event) == LV_EVENT_KEY) {
            const uint32_t key = lv_event_get_key(event);
            if (key == LV_KEY_ESC && self->back_) self->back_();
            else if ((key == LV_KEY_LEFT || key == LV_KEY_UP) && self->group_) lv_group_focus_prev(self->group_);
            else if ((key == LV_KEY_RIGHT || key == LV_KEY_DOWN) && self->group_) lv_group_focus_next(self->group_);
            return;
        }
        self->post([self, which] { self->press(which); });
    }

    void press(int which)
    {
        if (!session_) return;
        unsigned ev = 0;
        switch (which) {
        case kBack: ev = strokes_train_back(session_); break;
        case kSkip: ev = strokes_train_skip(session_); break;
        case kRedo: ev = strokes_train_redo(session_); break;
        case kReset: open_reset(); return;
        case kFinish:
            if (strokes_train_phase(session_) == STROKES_TRAIN_DONE) {
                restart();
                return;
            }
            ev = strokes_train_finish(session_);
            break;
        default: return;
        }
        apply(ev);
        flash(std::string(), STROKES_TRAIN_NONE);
    }

    /* Train again: a new session on the overlay as it is now (it resumes at the first glyph without templates) */
    void restart()
    {
        strokes_train_free(session_);
        std::string overlay;
        (void)read_file(overlay_path_, overlay);
        session_ = strokes_train_new(cp0_zone_ui_table(), cp0_zone_ui_config(), kFailing, overlay.c_str());
        attempts_ = 0;
        for (auto &k : kept_) k.clear();
        refresh();
        flash(std::string(), STROKES_TRAIN_NONE);
    }

    void open_reset()
    {
        cp0::ui::DialogSpec spec;
        spec.title = "Reset all personal templates?";
        spec.page = &page_;
        spec.buttons = {{"Cancel", cp0::ui::ButtonRole::Cancel, [this] { post([this] { close_dialog(); }); }},
                        {"Reset", cp0::ui::ButtonRole::Destructive, [this] { post([this] { confirm_reset(); }); }}};
        dialog_ = std::make_unique<cp0::ui::Dialog>(page_.root(), page_.layout(), std::move(spec));
        dialog_->add_text("The strokes you trained are removed from the recogniser; the shipped shapes stay. The "
                          "training log is kept.");
        dialog_->relayout();
        if (group_) {
            lv_group_add_obj(group_, dialog_->focus_object());
            lv_group_focus_obj(dialog_->focus_object());
        }
    }

    void close_dialog()
    {
        dialog_.reset();
        if (group_ && buttons_[kReset]) lv_group_focus_obj(buttons_[kReset]);
        keep_focus_visible();
    }

    void confirm_reset()
    {
        close_dialog();
        const unsigned ev = strokes_train_reset_all(session_);
        attempts_ = 0;
        apply(ev | STROKES_TRAIN_EV_MOVED);
        flash("Personal templates removed", STROKES_TRAIN_NONE);
    }

    void post(std::function<void()> callback)
    {
        posted_.push_back(std::move(callback));
        if (!post_armed_) post_armed_ = lv_async_call(run_posted_cb, this) == LV_RESULT_OK;
    }

    static void run_posted_cb(void *data)
    {
        auto *self = static_cast<StrokesTrainingPanel *>(data);
        if (!self) return;
        self->post_armed_ = false;
        std::vector<std::function<void()>> callbacks;
        callbacks.swap(self->posted_);
        for (auto &callback : callbacks) callback();
    }

    struct Thumb {
        lv_obj_t *box = nullptr;
        lv_obj_t *label = nullptr;
        Shape shape;
    };

    cp0::ui::Page &page_;
    lv_group_t *group_ = nullptr;
    std::function<void()> back_;
    strokes_train *session_ = nullptr;
    std::string overlay_path_, log_path_, error_;
    lv_obj_t *prompt_ = nullptr, *hint_ = nullptr, *stage_ = nullptr, *guide_box_ = nullptr, *result_ = nullptr;
    lv_obj_t *summary_ = nullptr;
    const lv_font_t *summary_font_ = nullptr;
    lv_obj_t *flash_ = nullptr;
    Shape guide_;
    std::array<Thumb, STROKES_TRAIN_REPS> thumbs_{};
    std::array<std::vector<float>, STROKES_TRAIN_REPS> kept_{};
    int attempts_ = 0;
    int side_ = 0, thumb_side_ = 0;
    std::array<lv_obj_t *, kButtons> buttons_{};
    std::array<lv_obj_t *, kButtons> button_label_{};
    std::unique_ptr<cp0::ui::Dialog> dialog_;
    std::vector<std::function<void()>> posted_;
    bool post_armed_ = false;
};

} // namespace

std::unique_ptr<SettingsNativePanel> settings_strokes_training_panel_create(const SettingsNativePanelContext &context)
{
    return std::make_unique<StrokesTrainingPanel>(context);
}

namespace settings_strokes {

void append_entry(Tree &tree, const NodeIter &system)
{
    if (!launcher_zone::active()) return; /* the PDA profile with its writing zone only */
    tree.append_child(system, SettingEntry{"Strokes training", settings_strokes_training_factory, PageType::Normal});
}

} // namespace settings_strokes
