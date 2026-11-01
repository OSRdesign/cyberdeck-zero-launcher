/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Updates (report 047 P4): the "working" message box of a long wait (settings_wait_model.hpp): a bordered block
 * in the page body, no overlay: the title ("Preparing the updates"), the step in plain words, a bar (stage progress, or a
 * moving segment when the length is unknown), the elapsed counter mm:ss, the slow-board line, the 20 s line, and "Esc
 * cancels" when the wait can be cancelled. Tokens of the Updates page: the border is the hold bar's track (#3A3A3A), the
 * bar #3A3A3A / #2D9CDB as the progress bar, the texts white and the Apps page's grey (#8A929B); where the block with its
 * longest texts would not fit under the status line (640x480's large fonts), the grey lines take the status line's
 * font (measured once, at construction: the fonts never change during a wait). Only what changed is
 * redrawn (a label's text, the bar's value): the page calls show() from its 200 ms timer and the elapsed label changes
 * once a second, the moving segment at most 4 times a second. Never focused: the page's list keeps the keys (Esc).
 * LVGL thread only.
 */
#pragma once

#include "lvgl/lvgl.h"

#include <string>

namespace settings_wait {

struct BoxLook {
    int x = 0, y = 0, w = 0;     /* the block on its parent (its height follows its content) */
    const lv_font_t *title_font = nullptr;
    const lv_font_t *text_font = nullptr;
    int bar_h = 6;
    int pad = 8;                 /* inside the border */
    int gap = 6;                 /* between the lines */
    /* the room under y (0: no limit) and the smaller font (the page's status line) that the grey lines, then the step and
     * the title take when the block with its longest texts would not fit (a 640x480 screen with large fonts) */
    int max_h = 0;
    const lv_font_t *small_font = nullptr;
};

struct BoxState {
    std::string title;
    std::string step;
    int percent = -1;            /* 0..100, -1: unknown (the moving segment) */
    long long elapsed_ms = 0;
    long long since_change_ms = 0; /* since the step last changed (the 20 s line) */
    bool slow = false;           /* a slow board: the reassurance line at once */
    bool cancellable = false;
};

class WaitBox {
public:
    WaitBox(lv_obj_t *parent, const BoxLook &look);
    ~WaitBox();
    WaitBox(const WaitBox &) = delete;
    WaitBox &operator=(const WaitBox &) = delete;

    void show(const BoxState &state);
    void hide();
    bool visible() const { return visible_; }

private:
    static void delete_cb(lv_event_t *event);
    void set_text(lv_obj_t *label, std::string &shown, const std::string &text);
    void fit(const BoxLook &look);

    lv_obj_t *box_ = nullptr;
    lv_obj_t *title_ = nullptr, *step_ = nullptr, *bar_ = nullptr, *elapsed_ = nullptr;
    lv_obj_t *slow_ = nullptr, *still_ = nullptr, *cancel_ = nullptr;
    std::string title_text_, step_text_, elapsed_text_, slow_text_, still_text_, cancel_text_;
    int bar_mode_ = -2; /* -1 the segment, else the value shown */
    int bar_start_ = -1, bar_end_ = -1;
    bool visible_ = false;
};

} // namespace settings_wait
