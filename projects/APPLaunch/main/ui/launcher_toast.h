/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl/lvgl.h"

class LauncherToast
{
public:
    void show(const char *text) noexcept;
    /* The same toast for duration_ms instead of the usual 1.5 s (a two-line notice: Settings > Updates' toasts). */
    void show_for(const char *text, uint32_t duration_ms) noexcept;
    void show_persistent(const char *text) noexcept;
    void hide();
    void shutdown();

private:
    bool ensure_created() noexcept;
    static void hide_timer_cb(lv_timer_t *timer) noexcept;
    static void container_delete_cb(lv_event_t *event) noexcept;
    static void label_delete_cb(lv_event_t *event) noexcept;

    lv_obj_t *container_ = nullptr;
    lv_obj_t *label_ = nullptr;
    lv_timer_t *hide_timer_ = nullptr;
};

LauncherToast &launcher_toast();
