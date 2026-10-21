/*
 * SPDX-License-Identifier: MIT
 *
 * Pointer navigation core and process-wide switch (see cp0_pointer_nav.h). Pure C11, no I/O.
 */

#include "cp0_pointer_nav.h"

#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define NEVER UINT64_MAX
#define Q 16 /* the smoothed level is kept in 1/16 counts */

/* ============================================================
 *  Parameters
 * ============================================================ */

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void cp0_ptr_nav_params_default(cp0_ptr_nav_params_t *p)
{
    if (!p) return;
    p->dead_zone = CP0_PTR_NAV_DEAD_ZONE;
    p->full_level = CP0_PTR_NAV_FULL_LEVEL;
    p->slow_rate_x10 = CP0_PTR_NAV_SLOW_RATE_X10;
    p->max_rate_x10 = CP0_PTR_NAV_MAX_RATE_X10;
    p->repeat_delay_ms = CP0_PTR_NAV_REPEAT_DELAY_MS;
    p->release_ms = CP0_PTR_NAV_RELEASE_MS;
    p->smooth_pct = CP0_PTR_NAV_SMOOTH_PCT;
    p->axis_switch_pct = CP0_PTR_NAV_AXIS_SWITCH_PCT;
    p->hold_ms = CP0_PTR_NAV_HOLD_MS;
    p->tap_slop = CP0_PTR_NAV_TAP_SLOP;
    p->click_guard_ms = CP0_PTR_NAV_CLICK_GUARD_MS;
    p->wheel_max = CP0_PTR_NAV_WHEEL_MAX;
    p->min_click_ms = CP0_PTR_NAV_MIN_CLICK_MS;
}

void cp0_ptr_nav_params_normalize(cp0_ptr_nav_params_t *p)
{
    if (!p) return;
    p->dead_zone = clampi(p->dead_zone, 1, 1000);
    p->full_level = clampi(p->full_level, p->dead_zone + 1, 10000);
    p->max_rate_x10 = clampi(p->max_rate_x10, 1, 1000);
    p->slow_rate_x10 = clampi(p->slow_rate_x10, 1, p->max_rate_x10);
    p->repeat_delay_ms = clampi(p->repeat_delay_ms, 0, 5000);
    p->release_ms = clampi(p->release_ms, 1, 5000);
    p->smooth_pct = clampi(p->smooth_pct, 1, 100);
    p->axis_switch_pct = clampi(p->axis_switch_pct, 100, 10000);
    p->hold_ms = clampi(p->hold_ms, 50, 10000);
    p->tap_slop = clampi(p->tap_slop, 0, 100000);
    p->click_guard_ms = clampi(p->click_guard_ms, 0, 5000);
    p->wheel_max = clampi(p->wheel_max, 1, 100);
    p->min_click_ms = clampi(p->min_click_ms, -1, p->hold_ms); /* -1 = auto; at hold_ms no click is left */
}

/* "123": 0 and the value, else -1 */
static int parse_uint(const char *s, int *out)
{
    if (!s || !*s) return -1;
    long v = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        if (v > 1000000) return -1;
    }
    *out = (int)v;
    return 0;
}

/* "12" or "12.5" (one decimal kept; never strtod: the locale may want a comma) -> x10 */
static int parse_x10(const char *s, int *out)
{
    if (!s || !*s) return -1;
    long v = 0;
    int digits = 0;
    for (; *s >= '0' && *s <= '9'; s++, digits++) {
        v = v * 10 + (*s - '0');
        if (v > 100000) return -1;
    }
    if (digits == 0) return -1;
    v *= 10;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        v += *s - '0';
        s++;
        while (*s >= '0' && *s <= '9') s++;
    }
    if (*s) return -1;
    *out = (int)v;
    return 0;
}

int cp0_ptr_nav_params_apply_env(cp0_ptr_nav_params_t *p, const char *(*get)(const char *name))
{
    if (!p || !get) return 0;
    static const struct {
        const char *name;
        size_t offset;
        int x10;
    } table[] = {
        {"APPLAUNCH_POINTER_DEADZONE", offsetof(cp0_ptr_nav_params_t, dead_zone), 0},
        {"APPLAUNCH_POINTER_FULL_LEVEL", offsetof(cp0_ptr_nav_params_t, full_level), 0},
        {"APPLAUNCH_POINTER_SLOW_RATE", offsetof(cp0_ptr_nav_params_t, slow_rate_x10), 1},
        {"APPLAUNCH_POINTER_MAX_RATE", offsetof(cp0_ptr_nav_params_t, max_rate_x10), 1},
        {"APPLAUNCH_POINTER_REPEAT_DELAY_MS", offsetof(cp0_ptr_nav_params_t, repeat_delay_ms), 0},
        {"APPLAUNCH_POINTER_RELEASE_MS", offsetof(cp0_ptr_nav_params_t, release_ms), 0},
        {"APPLAUNCH_POINTER_SMOOTH_PCT", offsetof(cp0_ptr_nav_params_t, smooth_pct), 0},
        {"APPLAUNCH_POINTER_AXIS_SWITCH_PCT", offsetof(cp0_ptr_nav_params_t, axis_switch_pct), 0},
        {"APPLAUNCH_POINTER_HOLD_MS", offsetof(cp0_ptr_nav_params_t, hold_ms), 0},
        {"APPLAUNCH_POINTER_TAP_SLOP", offsetof(cp0_ptr_nav_params_t, tap_slop), 0},
        {"APPLAUNCH_POINTER_CLICK_GUARD_MS", offsetof(cp0_ptr_nav_params_t, click_guard_ms), 0},
        {"APPLAUNCH_POINTER_MIN_CLICK_MS", offsetof(cp0_ptr_nav_params_t, min_click_ms), 0},
    };
    int taken = 0;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        const char *text = get(table[i].name);
        int value = 0;
        if (!text || !*text) continue;
        if ((table[i].x10 ? parse_x10(text, &value) : parse_uint(text, &value)) != 0) continue;
        memcpy((char *)p + table[i].offset, &value, sizeof(value));
        taken++;
    }
    cp0_ptr_nav_params_normalize(p);
    return taken;
}

/* Pads whose firmware turns a tap on the surface into a BTN_LEFT press. Add a row for another such pad. */
static const struct {
    unsigned vendor, product;     /* USB ids (I: Vendor= Product= in /proc/bus/input/devices) */
    const char *name_a, *name_b;  /* the kernel name contains both: the match when the ids are unknown */
    int min_click_ms;
} k_devices[] = {
    /* ZitaoTech HACKBerryPiQ20 (HackBerry CM5 keyboard), its "Mouse" interface: taps 0.16-0.19 s, button >= 0.35 s */
    {0x1209u, 0x0b20u, "HACKBerryPiQ20", "Mouse", CP0_PTR_NAV_MIN_CLICK_MS_HACKBERRY},
};

int cp0_ptr_nav_device_min_click_ms(unsigned vendor, unsigned product, const char *name)
{
    for (size_t i = 0; i < sizeof(k_devices) / sizeof(k_devices[0]); i++) {
        if (vendor || product) {
            if (vendor == k_devices[i].vendor && product == k_devices[i].product) return k_devices[i].min_click_ms;
        } else if (name && strstr(name, k_devices[i].name_a) && strstr(name, k_devices[i].name_b)) {
            return k_devices[i].min_click_ms;
        }
    }
    return -1;
}

void cp0_ptr_nav_params_for_device(const cp0_ptr_nav_params_t *base, unsigned vendor, unsigned product,
                                   const char *name, cp0_ptr_nav_params_t *out)
{
    if (!out) return;
    if (base) *out = *base;
    else cp0_ptr_nav_params_default(out);
    if (out->min_click_ms < 0) {
        const int table = cp0_ptr_nav_device_min_click_ms(vendor, product, name);
        out->min_click_ms = table > 0 ? table : 0;
    }
    cp0_ptr_nav_params_normalize(out);
}

const char *cp0_ptr_key_name(cp0_ptr_key_t key)
{
    switch (key) {
    case CP0_PTR_KEY_UP: return "UP";
    case CP0_PTR_KEY_DOWN: return "DOWN";
    case CP0_PTR_KEY_LEFT: return "LEFT";
    case CP0_PTR_KEY_RIGHT: return "RIGHT";
    case CP0_PTR_KEY_ENTER: return "ENTER";
    case CP0_PTR_KEY_OPTIONS: return "OPTIONS";
    case CP0_PTR_KEY_PAGE_UP: return "PAGE_UP";
    case CP0_PTR_KEY_PAGE_DOWN: return "PAGE_DOWN";
    default: return "NONE";
    }
}

/* ============================================================
 *  State machine
 * ============================================================ */

typedef struct {
    cp0_ptr_key_t *out;
    int max;
    int n;
} sink_t;

static void emit(sink_t *sink, cp0_ptr_key_t key)
{
    if (sink->out && sink->n < sink->max) sink->out[sink->n] = key;
    if (sink->n < sink->max) sink->n++;
}

void cp0_ptr_nav_init(cp0_ptr_nav_t *s, const cp0_ptr_nav_params_t *params)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (params) s->p = *params;
    else cp0_ptr_nav_params_default(&s->p);
    cp0_ptr_nav_params_normalize(&s->p);
}

void cp0_ptr_nav_reset(cp0_ptr_nav_t *s)
{
    if (!s) return;
    const cp0_ptr_nav_params_t p = s->p;
    const uint64_t now = s->now;
    memset(s, 0, sizeof(*s));
    s->p = p;
    s->now = now;
}

static int level_q(const cp0_ptr_nav_t *s, int axis)
{
    return abs(axis ? s->vy_q : s->vx_q);
}

static int sign_of(int v)
{
    return v < 0 ? -1 : 1;
}

/* the shortest time between two steps: MAX_RATE, rounded up */
static uint64_t min_interval_ms(const cp0_ptr_nav_t *s)
{
    return (uint64_t)((10000 + s->p.max_rate_x10 - 1) / s->p.max_rate_x10);
}

/* time between two repeat steps at level lq (1/16 counts per report) */
static uint64_t period_ms(const cp0_ptr_nav_t *s, int lq)
{
    const int dzq = s->p.dead_zone * Q, fullq = s->p.full_level * Q;
    int rate = s->p.slow_rate_x10;
    if (lq >= fullq) rate = s->p.max_rate_x10;
    else if (lq > dzq)
        rate = s->p.slow_rate_x10 +
               (int)((int64_t)(s->p.max_rate_x10 - s->p.slow_rate_x10) * (lq - dzq) / (fullq - dzq));
    uint64_t period = (uint64_t)((10000 + rate / 2) / rate);
    const uint64_t floor = min_interval_ms(s);
    return period < floor ? floor : period;
}

static int nav_suppressed(const cp0_ptr_nav_t *s)
{
    return s->down && !s->drag; /* the finger rests on the pad for a click or a hold */
}

static void emit_step(cp0_ptr_nav_t *s, sink_t *sink)
{
    cp0_ptr_key_t key;
    if (s->axis == 0) key = s->dir > 0 ? CP0_PTR_KEY_RIGHT : CP0_PTR_KEY_LEFT;
    else key = s->dir > 0 ? CP0_PTR_KEY_DOWN : CP0_PTR_KEY_UP;
    emit(sink, key);
    s->last_step = s->now;
    s->stepped = 1;
    s->push_steps++;
}

/* the delay after a step: REPEAT_DELAY after the first of a push, then the rate of the level; never under the cap */
static uint64_t after_step_ms(const cp0_ptr_nav_t *s)
{
    uint64_t wait = s->push_steps <= 1 ? (uint64_t)s->p.repeat_delay_ms : period_ms(s, level_q(s, s->axis));
    const uint64_t floor = min_interval_ms(s);
    return wait < floor ? floor : wait;
}

/* a push starts (or turns to the other axis, or reverses): first step at once unless the rate cap says wait */
static void start_push(cp0_ptr_nav_t *s, sink_t *sink, int axis)
{
    s->active = 1;
    s->axis = axis;
    s->dir = sign_of(axis ? s->vy_q : s->vx_q);
    s->push_steps = 0;
    const uint64_t earliest = s->stepped ? s->last_step + min_interval_ms(s) : s->now;
    if (earliest <= s->now) {
        emit_step(s, sink);
        s->next_step = s->now + after_step_ms(s);
    } else {
        s->next_step = earliest; /* the cap holds the first step back a little */
    }
}

static void update_nav(cp0_ptr_nav_t *s, sink_t *sink)
{
    const int dzq = s->p.dead_zone * Q;
    const int ax = level_q(s, 0), ay = level_q(s, 1);
    const int strongest = ax > ay ? ax : ay;
    if (strongest >= dzq) {
        s->motion_seen = 1;
        s->last_motion = s->now;
    }
    if (nav_suppressed(s)) {
        s->active = 0;
        return;
    }
    if (!s->active) {
        if (strongest >= dzq) start_push(s, sink, ax >= ay ? 0 : 1);
        return;
    }
    const int cur = level_q(s, s->axis), other = level_q(s, !s->axis);
    if (other >= dzq && (int64_t)other * 100 > (int64_t)cur * s->p.axis_switch_pct) {
        start_push(s, sink, !s->axis);
        return;
    }
    if (cur < dzq) {
        s->active = 0; /* back under the dead zone: released */
        return;
    }
    if (sign_of(s->axis ? s->vy_q : s->vx_q) != s->dir) start_push(s, sink, s->axis);
}

static void press(cp0_ptr_nav_t *s)
{
    s->down = 1;
    s->press_at = s->now;
    s->moved = 0;
    s->press_dx = 0;
    s->press_dy = 0;
    s->drag = 0;
    s->hold_fired = 0;
    s->guarded = s->motion_seen && s->now - s->last_motion < (uint64_t)s->p.click_guard_ms;
    s->active = 0;
}

static void release(cp0_ptr_nav_t *s, sink_t *sink)
{
    if (!s->guarded && !s->drag && !s->hold_fired) {
        const uint64_t held = s->now - s->press_at;
        if (s->p.min_click_ms > 0 && held < (uint64_t)s->p.min_click_ms) { /* a firmware tap, not the button */
            s->taps_ignored++;
            s->last_tap_ms = (uint32_t)held;
        } else {
            emit(sink, CP0_PTR_KEY_ENTER);
        }
    }
    s->down = 0;
    s->guarded = 0;
    s->drag = 0;
    s->hold_fired = 0;
    s->moved = 0;
    s->press_dx = 0;
    s->press_dy = 0;
}

static uint64_t deadline_release(const cp0_ptr_nav_t *s)
{
    return s->have_report && (s->active || s->vx_q || s->vy_q) ? s->last_report + (uint64_t)s->p.release_ms : NEVER;
}

static uint64_t deadline_step(const cp0_ptr_nav_t *s)
{
    return s->active && !nav_suppressed(s) ? s->next_step : NEVER;
}

static uint64_t deadline_hold(const cp0_ptr_nav_t *s)
{
    return s->down && !s->guarded && !s->drag && !s->hold_fired ? s->press_at + (uint64_t)s->p.hold_ms : NEVER;
}

/* fire every timer due at or before `target`, each at its own time */
static void fire_due(cp0_ptr_nav_t *s, sink_t *sink, uint64_t target)
{
    for (int guard = 0; guard < 256; guard++) {
        const uint64_t r = deadline_release(s), st = deadline_step(s), h = deadline_hold(s);
        uint64_t d = r < st ? r : st;
        if (h < d) d = h;
        if (d == NEVER || d > target) return;
        if (d > s->now) s->now = d;
        if (r == d) { /* released first: a step due at the same time does not come */
            s->vx_q = 0;
            s->vy_q = 0;
            s->active = 0;
        } else if (h == d) {
            emit(sink, CP0_PTR_KEY_OPTIONS);
            s->hold_fired = 1;
        } else {
            emit_step(s, sink);
            s->next_step = s->now + after_step_ms(s);
        }
    }
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

/* v moved pct % of the way to target, rounded to the nearest (a truncating step never reaches a steady target: a
 * steady push of exactly the dead zone would never count) */
static int smooth(int v, int target, int pct)
{
    const int64_t d = (int64_t)(target - v) * pct;
    return v + (int)(d >= 0 ? (d + 50) / 100 : -((-d + 50) / 100));
}

static void apply_report(cp0_ptr_nav_t *s, sink_t *sink, const cp0_ptr_report_t *r)
{
    const int notches = clampi(r->wheel, -s->p.wheel_max, s->p.wheel_max);
    for (int i = 0; i < iabs(notches); i++) emit(sink, notches > 0 ? CP0_PTR_KEY_UP : CP0_PTR_KEY_DOWN);

    if (r->button == CP0_PTR_BUTTON_DOWN && !s->down) press(s);

    if (r->dx != 0 || r->dy != 0) {
        const int dx = clampi(r->dx, -100000, 100000), dy = clampi(r->dy, -100000, 100000);
        if (s->down) {
            /* how far the finger went since the press (net, so jitter back and forth cancels out) */
            s->press_dx = clampi(s->press_dx + dx, -1000000, 1000000);
            s->press_dy = clampi(s->press_dy + dy, -1000000, 1000000);
            s->moved = iabs(s->press_dx) + iabs(s->press_dy);
            if (!s->drag && s->moved > s->p.tap_slop) s->drag = 1; /* a drag: no click, no options */
        }
        s->vx_q = smooth(s->vx_q, dx * Q, s->p.smooth_pct);
        s->vy_q = smooth(s->vy_q, dy * Q, s->p.smooth_pct);
        s->have_report = 1;
        s->last_report = s->now;
        update_nav(s, sink);
    }

    if (r->button == CP0_PTR_BUTTON_UP && s->down) release(s, sink);
}

int cp0_ptr_nav_step(cp0_ptr_nav_t *s, uint32_t dt_ms, const cp0_ptr_report_t *report, cp0_ptr_key_t *out, int max)
{
    if (!s) return 0;
    sink_t sink = {out, max < 0 ? 0 : max, 0};
    const uint64_t target = s->now + dt_ms;
    fire_due(s, &sink, target);
    s->now = target;
    if (report) apply_report(s, &sink, report);
    return sink.n;
}

int cp0_ptr_nav_next_ms(const cp0_ptr_nav_t *s)
{
    if (!s) return -1;
    const uint64_t r = deadline_release(s), st = deadline_step(s), h = deadline_hold(s);
    uint64_t d = r < st ? r : st;
    if (h < d) d = h;
    if (d == NEVER) return -1;
    if (d <= s->now) return 0;
    return d - s->now > (uint64_t)INT_MAX ? INT_MAX : (int)(d - s->now);
}

int cp0_ptr_nav_busy(const cp0_ptr_nav_t *s)
{
    return s && (s->active || s->down);
}

/* ============================================================
 *  Process-wide switch
 * ============================================================ */

typedef void (*wake_hook_t)(void);

static atomic_int s_override = -1;
static _Atomic(wake_hook_t) s_wake_hook = NULL;

void cp0_pointer_nav_set_enabled(int enabled)
{
    const int value = enabled ? 1 : 0;
    const int previous = atomic_exchange(&s_override, value);
    const wake_hook_t hook = atomic_load(&s_wake_hook);
    if (previous != value && hook) hook();
}

int cp0_pointer_nav_get_override(void)
{
    return atomic_load(&s_override);
}

void cp0_pointer_nav_set_wake_hook(void (*hook)(void))
{
    atomic_store(&s_wake_hook, hook);
}
