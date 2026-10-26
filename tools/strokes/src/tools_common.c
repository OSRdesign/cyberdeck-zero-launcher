/*
 * SPDX-License-Identifier: MIT
 *
 * Helpers shared by the command-line tools of tools/strokes.
 */
#include "tools_common.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int readable(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

const char *tools_default_table(const char *argv0, char *buf, size_t size)
{
    const char *env = getenv("STROKES_TABLE");
    if (env && *env)
        return env;
    if (readable(STROKES_SHARE_DIR "/glyphs.txt"))
        return STROKES_SHARE_DIR "/glyphs.txt";
    const char *slash = argv0 ? strrchr(argv0, '/') : NULL;
    if (slash) {
        snprintf(buf, size, "%.*s/../../" STROKES_SHARE_DIR "/glyphs.txt", (int)(slash - argv0), argv0);
        if (readable(buf))
            return buf;
    }
    return STROKES_SHARE_DIR "/glyphs.txt";
}

strokes_table *tools_load_table(const char *table, const char **overlays, int n_overlays)
{
    strokes_table *t = strokes_table_new();
    strokes_error err;
    if (!t) {
        fprintf(stderr, "out of memory\n");
        return NULL;
    }
    if (strokes_table_add_file(t, table, &err)) {
        fprintf(stderr, "%s:%d: %s\n", err.source, err.line, err.msg);
        strokes_table_free(t);
        return NULL;
    }
    for (int i = 0; i < n_overlays; ++i)
        if (strokes_table_add_file(t, overlays[i], &err)) {
            fprintf(stderr, "%s:%d: %s\n", err.source, err.line, err.msg);
            strokes_table_free(t);
            return NULL;
        }
    return t;
}

static const struct {
    const char *name;
    size_t off;
    int is_int;
} cfg_fields[] = {
    {"ppmm", offsetof(strokes_config, ppmm), 0},
    {"heal_ms", offsetof(strokes_config, heal_ms), 0},
    {"heal_mm", offsetof(strokes_config, heal_mm), 0},
    {"heal_speed", offsetof(strokes_config, heal_speed), 1},
    {"tap_max_mm", offsetof(strokes_config, tap_max_mm), 0},
    {"tap_max_ms", offsetof(strokes_config, tap_max_ms), 0},
    {"min_stroke_mm", offsetof(strokes_config, min_stroke_mm), 0},
    {"min_score", offsetof(strokes_config, min_score), 0},
    {"min_margin", offsetof(strokes_config, min_margin), 0},
    {"rot_tol", offsetof(strokes_config, rot_tol_deg), 0},
    {"aspect_tol", offsetof(strokes_config, aspect_tol), 0},
    {"smooth", offsetof(strokes_config, smooth), 1},
    {"hook_trim", offsetof(strokes_config, hook_trim), 1},
};

int tools_cfg_set(strokes_config *c, const char *kv)
{
    const char *eq = strchr(kv, '=');
    if (!eq)
        return -1;
    size_t kl = (size_t)(eq - kv);
    char *end = NULL;
    double v = strtod(eq + 1, &end);
    if (!end || *end || !isfinite(v))
        return -1;
    for (size_t i = 0; i < sizeof cfg_fields / sizeof cfg_fields[0]; ++i)
        if (strlen(cfg_fields[i].name) == kl && strncmp(kv, cfg_fields[i].name, kl) == 0) {
            if (cfg_fields[i].is_int)
                *(int *)((char *)c + cfg_fields[i].off) = (int)v;
            else
                *(float *)((char *)c + cfg_fields[i].off) = (float)v;
            return 0;
        }
    return -1;
}

void tools_cfg_describe(const strokes_config *c, char *buf, size_t size)
{
    snprintf(buf, size,
             "ppmm %.2f, min_score %.2f, min_margin %.3f, slant tolerance %.0f deg, aspect tolerance %.0f %%, heal %.0f ms / %.1f mm%s, tap < %.1f mm and < %.0f ms, "
             "min stroke %.1f mm, smoothing %s, hook trim %s",
             (double)c->ppmm, (double)c->min_score, (double)c->min_margin, (double)c->rot_tol_deg,
             (double)(c->aspect_tol * 100.0f), (double)c->heal_ms, (double)c->heal_mm,
             c->heal_speed ? " + reach at pen speed" : "", (double)c->tap_max_mm, (double)c->tap_max_ms,
             (double)c->min_stroke_mm, c->smooth ? "on" : "off", c->hook_trim ? "on" : "off");
}

int tools_label_set(const strokes_table *t, const char *id, int box)
{
    int want[4] = {box == STROKES_BOX_DIGITS ? STROKES_SET_DIGITS : STROKES_SET_LETTERS, STROKES_SET_ANY,
                   STROKES_SET_PUNCT, STROKES_SET_EXT};
    for (int w = 0; w < 4; ++w) {
        char gid[STROKES_ID_MAX];
        int set;
        for (int i = 0; strokes_table_glyph_info(t, i, gid, &set, NULL) == 0; ++i)
            if (set == want[w] && strcmp(gid, id) == 0)
                return set;
    }
    char gid[STROKES_ID_MAX];
    int set;
    for (int i = 0; strokes_table_glyph_info(t, i, gid, &set, NULL) == 0; ++i)
        if (strcmp(gid, id) == 0)
            return set;
    return -1;
}
