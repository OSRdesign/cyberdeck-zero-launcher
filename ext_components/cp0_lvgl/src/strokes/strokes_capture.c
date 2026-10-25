/*
 * SPDX-License-Identifier: MIT
 *
 * Stroke clean-up (report 038 section 5, steps 2-4): tap / dwell / too-short test, distance decimation (kills the
 * jitter of a slow or resting stylus), fine resampling, landing and lift hook trimming and smoothing. The matcher
 * resamples the fine path to STK_N points itself. Drop-out healing happens earlier, in the engine.
 */
#include "strokes_internal.h"

#include <math.h>

#define STEP_MM 0.35f     /* decimation: keep raw points at least this far apart */
#define HOOK_FRAC 0.08f   /* a hook is at most this fraction of the path ... */
#define HOOK_MAX_MM 2.5f  /* ... and at most this long */

int stk_clean(const strokes_config *cfg, const strokes_point *raw, int n, float duration_ms, stk_vec fine[STK_FINE],
              int *nfine, float *path_mm)
{
    float ppmm = cfg->ppmm > 0.0f ? cfg->ppmm : 8.54f;
    *path_mm = 0.0f;
    *nfine = 1;
    if (n <= 0) {
        fine[0].x = fine[0].y = 0.0f;
        return STK_CLEAN_SHORT;
    }
    float minx = raw[0].x, maxx = raw[0].x, miny = raw[0].y, maxy = raw[0].y;
    for (int i = 1; i < n; ++i) {
        minx = fminf(minx, raw[i].x);
        maxx = fmaxf(maxx, raw[i].x);
        miny = fminf(miny, raw[i].y);
        maxy = fmaxf(maxy, raw[i].y);
    }
    float extent = fmaxf(maxx - minx, maxy - miny);
    if (extent < cfg->tap_max_mm * ppmm) {
        fine[0].x = 0.5f * (minx + maxx);
        fine[0].y = 0.5f * (miny + maxy);
        *path_mm = extent / ppmm;
        return duration_ms <= cfg->tap_max_ms ? STK_CLEAN_TAP : STK_CLEAN_DWELL;
    }

    static const int cap = STK_MAX_RAW;
    stk_vec dec[STK_MAX_RAW];
    float step = STEP_MM * ppmm;
    int nd = 0;
    dec[nd++] = (stk_vec){raw[0].x, raw[0].y};
    for (int i = 1; i < n && nd < cap; ++i) {
        stk_vec p = {raw[i].x, raw[i].y};
        if (hypotf(p.x - dec[nd - 1].x, p.y - dec[nd - 1].y) >= step)
            dec[nd++] = p;
    }
    stk_vec last = {raw[n - 1].x, raw[n - 1].y};
    if (nd < cap && (last.x != dec[nd - 1].x || last.y != dec[nd - 1].y))
        dec[nd++] = last;
    float path_px = stk_path_len(dec, nd);
    *path_mm = path_px / ppmm;
    stk_resample(dec, nd, fine, STK_FINE);
    *nfine = STK_FINE;
    if (*path_mm < cfg->min_stroke_mm)
        return STK_CLEAN_SHORT;

    int nf = STK_FINE;
    if (cfg->hook_trim) {
        float max_hook = fminf(HOOK_FRAC * path_px, HOOK_MAX_MM * ppmm);
        nf = stk_trim_hooks(fine, nf, max_hook);
    }
    if (cfg->smooth)
        stk_smooth(fine, nf);
    *nfine = nf;
    *path_mm = stk_path_len(fine, nf) / ppmm;
    return STK_CLEAN_STROKE;
}
