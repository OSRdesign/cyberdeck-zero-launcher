/*
 * SPDX-License-Identifier: MIT
 *
 * The lifecycle of a cp0_lvgl app (see cp0_app_lifecycle_lvgl.h): the protocol (cp0_app_lifecycle.c) driven from the
 * LVGL loop, the LVGL side of hide and show, the key filter. Platform neutral: the board's port is
 * src/cp0/cp0_app_lifecycle_device.cpp, the apps' PC harness has its own.
 */
#include "cp0_app_lifecycle_lvgl.h"

#include "cp0_app_lifecycle.h"
#include "cp0_keyboard_queue.h"
#include "keyboard_input.h"
#include "lvgl/lvgl.h"

#include <stdio.h>
#include <time.h>

static cp0_app_lifecycle_t s_lc = {.fd = -1};
static const cp0_app_lifecycle_port_t *s_port;
static void (*s_on_close)(void);
static cp0_app_lc_keys_t s_keys;
static cp0_keyboard_key_filter_t s_prev_filter;
static int s_filter_on;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int key_filter(const struct key_item *item)
{
    if (item && cp0_app_lifecycle_key_drop(&s_keys, s_lc.hidden, item->key_code, item->key_state,
                                           (item->mods & KBD_MOD_ALT) != 0))
        return 1;
    return s_prev_filter ? s_prev_filter(item) : 0;
}

static void remove_filter(void)
{
    if (!s_filter_on) return;
    if (cp0_keyboard_get_key_filter() == key_filter) cp0_keyboard_set_key_filter(s_prev_filter);
    s_prev_filter = NULL;
    s_filter_on = 0;
}

/* on 0: no display renders while hidden: what is already marked is dropped and nothing can be marked any more (a paused
 * refresh timer would not do: LVGL resumes it on every invalidation); 1: marking again (LVGL counts these calls: the
 * image widget nests its own pairs). The show then invalidates everything. */
static void rendering(int on)
{
    for (lv_display_t *d = lv_display_get_next(NULL); d; d = lv_display_get_next(d)) {
        if (!on) (void)lv_inv_area(d, NULL); /* the areas marked before the hide: never drawn */
        lv_display_enable_invalidation(d, on != 0);
    }
}

static void invalidate(lv_obj_t *obj)
{
    if (obj) lv_obj_invalidate(obj);
}

/* the next refresh repaints every display completely */
static void invalidate_all(void)
{
    for (lv_display_t *d = lv_display_get_next(NULL); d; d = lv_display_get_next(d)) {
        invalidate(lv_display_get_screen_active(d));
        invalidate(lv_display_get_layer_bottom(d));
        invalidate(lv_display_get_layer_top(d));
        invalidate(lv_display_get_layer_sys(d));
    }
}

static void on_hide(void *ctx, uint32_t n)
{
    (void)ctx;
    const double t0 = now_ms();
    if (s_port && s_port->output) s_port->output(0); /* first: from here on no pixel reaches the framebuffer */
    rendering(0);
    lv_indev_reset(NULL, NULL); /* the press in progress (the three fingers of G1 are still down) ends without a click */
    if (s_port && s_port->input) s_port->input(0);
    cp0_keyboard_queue_clear();
    fprintf(stderr, "[lifecycle] hide %u: hidden (%.1f ms)\n", (unsigned)n, now_ms() - t0);
}

static void on_show(void *ctx, uint32_t n)
{
    (void)ctx;
    const double t0 = now_ms();
    if (s_port && s_port->input) s_port->input(1);
    cp0_keyboard_queue_clear(); /* what was typed while hidden */
    cp0_app_lifecycle_keys_shown(&s_keys);
    lv_indev_reset(NULL, NULL);
    rendering(1);
    invalidate_all();
    if (s_port && s_port->output) s_port->output(1);
    fprintf(stderr, "[lifecycle] show %u: visible, full repaint (%.1f ms)\n", (unsigned)n, now_ms() - t0);
}

static void on_close(void *ctx, uint32_t n)
{
    (void)ctx;
    fprintf(stderr, "[lifecycle] close %u: quitting%s\n", (unsigned)n, s_lc.hidden ? " (hidden)" : "");
    if (s_on_close) s_on_close();
}

static const cp0_app_lifecycle_ops_t s_ops = {on_hide, on_show, on_close};

int cp0_app_lifecycle_lvgl_start(const cp0_app_lifecycle_port_t *port, void (*on_close_fn)(void))
{
    if (s_lc.fd >= 0) return 1;
    if (!cp0_app_lifecycle_init(&s_lc, &s_ops, NULL)) return 0;
    s_port = port;
    s_on_close = on_close_fn;
    s_keys = (cp0_app_lc_keys_t){{0}, 0};
    s_prev_filter = cp0_keyboard_get_key_filter();
    cp0_keyboard_set_key_filter(key_filter);
    s_filter_on = 1;
    fprintf(stderr, "[lifecycle] on: control socket %d (hide / show / close; Alt+Tab is the launcher's)\n", s_lc.fd);
    return 1;
}

void cp0_app_lifecycle_lvgl_service(void)
{
    if (s_lc.fd < 0) return;
    if (cp0_app_lifecycle_service(&s_lc) < 0) {
        fprintf(stderr, "[lifecycle] the launcher closed the control socket%s\n",
                s_lc.closing ? " while the app was hidden: quitting" : ": the app stays as it is");
        if (!s_lc.hidden) remove_filter();
    }
}

int cp0_app_lifecycle_lvgl_active(void)
{
    return s_lc.fd >= 0;
}

int cp0_app_lifecycle_lvgl_hidden(void)
{
    return s_lc.hidden;
}

int cp0_app_lifecycle_lvgl_send(const char *msg)
{
    return cp0_app_lifecycle_send(&s_lc, msg);
}

void cp0_app_lifecycle_lvgl_stop(void)
{
    remove_filter();
    cp0_app_lifecycle_close(&s_lc);
    s_port = NULL;
    s_on_close = NULL;
}
