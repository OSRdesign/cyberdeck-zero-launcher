/*
 * SPDX-License-Identifier: MIT
 *
 * Shared renderer of the launcher's status bar (see cp0_statusbar.h).
 */
#define _GNU_SOURCE
#include "cp0_statusbar.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* ---- the home grid's numbers (native_ui.cpp) */
#define PILL_W 104
#define PILL_H 40
#define PILL_RADIUS 10
#define EDGE 16                 /* distance of the pill from the right edge */
#define PILL_COLOR 0x8A5A2Bu    /* kClockBg */
#define WIFI_W 44
#define WIFI_H 30
#define WIFI_GAP 16
#define WIFI_Y_OFFSET 5         /* wifi block top = pill top + 5 (13 when the pill is at 8) */
#define BAR_W 8
#define BAR_PITCH 12
#define BAR_RADIUS 3
#define BT_GAP 16
#define BT_Y_OFFSET 4           /* glyph label top = pill top + 4 (12 when the pill is at 8) */
#define FONT_PX 28
#define BAR_ON 0x33CC33u        /* kBarOn */
#define BAR_OFF 0x4D4D4Du       /* kBarOff */
#define BT_CONNECTED 0x3B9DFFu  /* kBtConnected */
#define BT_IDLE 0x5A5A5Au       /* kBtIdle */
#define BT_CODEPOINT 0xF293     /* LV_SYMBOL_BLUETOOTH */

static const int kBarHeights[4] = {9, 15, 21, 28};
static const int kBarThresholds[4] = {1, 30, 60, 80};

struct cp0_statusbar {
    FT_Library library;
    FT_Face text_face;
    FT_Face icon_face;
};

cp0_statusbar_t *cp0_statusbar_create(const char *text_font, const char *icon_font)
{
    cp0_statusbar_t *bar = calloc(1, sizeof(*bar));
    if (!bar) return NULL;
    if (FT_Init_FreeType(&bar->library) != 0) { free(bar); return NULL; }
    if (FT_New_Face(bar->library, text_font, 0, &bar->text_face) != 0) {
        FT_Done_FreeType(bar->library);
        free(bar);
        return NULL;
    }
    FT_Set_Pixel_Sizes(bar->text_face, 0, FONT_PX);
    if (icon_font && FT_New_Face(bar->library, icon_font, 0, &bar->icon_face) == 0)
        FT_Set_Pixel_Sizes(bar->icon_face, 0, FONT_PX);
    return bar;
}

void cp0_statusbar_destroy(cp0_statusbar_t *bar)
{
    if (!bar) return;
    if (bar->icon_face) FT_Done_Face(bar->icon_face);
    if (bar->text_face) FT_Done_Face(bar->text_face);
    FT_Done_FreeType(bar->library);
    free(bar);
}

/* ------------------------------------------------------------------------------- state */

static int read_first_line(const char *path, char *out, size_t size)
{
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    const int ok = fgets(out, (int)size, file) != NULL;
    fclose(file);
    return ok;
}

void cp0_statusbar_read_state(cp0_statusbar_state_t *state)
{
    memset(state, 0, sizeof(*state));
    char line[160];
    if (read_first_line("/sys/class/net/wlan0/operstate", line, sizeof(line)) && strncmp(line, "up", 2) == 0)
        state->wifi_up = 1;
    if (state->wifi_up) {
        FILE *file = fopen("/proc/net/wireless", "r");
        while (file && fgets(line, sizeof(line), file)) {
            const char *colon = strstr(line, "wlan0:");
            float link = 0, level = 0;
            if (colon && sscanf(colon + 6, "%*s %f %f", &link, &level) == 2) {
                int pct = (int)(level < 0 ? 2 * (level + 100) : level);    /* dBm -> percent, as native_ui does */
                state->wifi_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
                break;
            }
        }
        if (file) fclose(file);
    }
    DIR *dir = opendir("/sys/class/bluetooth");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            if (strncmp(entry->d_name, "hci", 3) != 0) continue;
            if (strchr(entry->d_name, ':')) state->bt_connected = 1;      /* hci0:11 = a connected device */
            else state->bt_on = 1;
        }
        closedir(dir);
    }
    if (state->bt_on) {                                                   /* switched off in rfkill? */
        DIR *rf = opendir("/sys/class/rfkill");
        struct dirent *entry;
        while (rf && (entry = readdir(rf))) {
            char path[320], kind[32] = "", soft[8] = "1";
            snprintf(path, sizeof(path), "/sys/class/rfkill/%.200s/type", entry->d_name);
            if (!read_first_line(path, kind, sizeof(kind)) || strncmp(kind, "bluetooth", 9) != 0) continue;
            snprintf(path, sizeof(path), "/sys/class/rfkill/%.200s/soft", entry->d_name);
            if (read_first_line(path, soft, sizeof(soft)) && soft[0] != '0') state->bt_on = 0;
        }
        if (rf) closedir(rf);
    }
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(state->clock, sizeof(state->clock), "%H:%M", &local);
}

/* ------------------------------------------------------------------------------ drawing */

static void blend(uint32_t *p, int width, int stride, int x, int y, uint32_t rgb, int alpha)
{
    if (alpha <= 0 || x < 0 || y < 0 || x >= width || y >= CP0_STATUSBAR_HEIGHT + 16) return;
    uint32_t *dst = &p[(size_t)y * stride + x];
    const uint32_t d = *dst;
    const uint32_t da = d >> 24;
    /* "source over" on a possibly transparent destination (straight alpha) */
    const uint32_t out_a = alpha + da * (255 - alpha) / 255;
    if (out_a == 0) return;
    uint32_t out = out_a << 24;
    for (int shift = 0; shift <= 16; shift += 8) {
        const uint32_t sc = (rgb >> shift) & 255, dc = (d >> shift) & 255;
        const uint32_t c = (sc * alpha * 255 + dc * da * (255 - alpha)) / (out_a * 255);
        out |= (c > 255 ? 255 : c) << shift;
    }
    *dst = out;
}

/* Anti-aliased rounded rectangle (4x4 supersampling of the corners). */
static void round_rect(uint32_t *p, int width, int stride, int x0, int y0, int w, int h, int r, uint32_t rgb, int alpha)
{
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int cx = x < r ? r : (x >= w - r ? w - r - 1 : -1);
            const int cy = y < r ? r : (y >= h - r ? h - r - 1 : -1);
            int coverage = 16;
            if (cx >= 0 && cy >= 0) {
                coverage = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const float fx = x + (sx + 0.5f) / 4 - (cx + 0.5f);
                        const float fy = y + (sy + 0.5f) / 4 - (cy + 0.5f);
                        if (fx * fx + fy * fy <= (float)r * r) ++coverage;
                    }
            }
            blend(p, width, stride, x0 + x, y0 + y, rgb, alpha * coverage / 16);
        }
}

/* Draws one glyph, its baseline at `baseline`; returns the advance in pixels. */
static int draw_glyph(FT_Face face, unsigned long codepoint, uint32_t *p, int width, int stride, int pen_x, int baseline, uint32_t rgb)
{
    if (!face || FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) return 0;
    const FT_GlyphSlot slot = face->glyph;
    for (unsigned row = 0; row < slot->bitmap.rows; ++row)
        for (unsigned col = 0; col < slot->bitmap.width; ++col) {
            const int a = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
            blend(p, width, stride, pen_x + slot->bitmap_left + (int)col, baseline - slot->bitmap_top + (int)row, rgb, a);
        }
    return (int)(slot->advance.x >> 6);
}

static int text_advance(FT_Face face, const char *text)
{
    int total = 0;
    for (; *text; ++text)
        if (FT_Load_Char(face, (unsigned char)*text, FT_LOAD_DEFAULT) == 0) total += (int)(face->glyph->advance.x >> 6);
    return total;
}

void cp0_statusbar_render(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                          int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state)
{
    if (!bar || !argb || !state) return;
    const int pill_x = width - EDGE - PILL_W - shift_left;
    const int pill_y = top;
    const int wifi_x = pill_x - WIFI_GAP - WIFI_W;
    const int wifi_y = pill_y + WIFI_Y_OFFSET;
    const int bt_right = wifi_x - BT_GAP;

    if (backing_alpha > 0)
        round_rect(argb, width, stride_px, bt_right - 34, pill_y - 2, pill_x + PILL_W + 6 - (bt_right - 34), PILL_H + 4, 12, 0x000000u, backing_alpha);

    /* clock pill with the time centred like an lv_label (line height = ascender - descender) */
    round_rect(argb, width, stride_px, pill_x, pill_y, PILL_W, PILL_H, PILL_RADIUS, PILL_COLOR, 255);
    const FT_Size_Metrics *m = &bar->text_face->size->metrics;
    const int ascent = (int)((m->ascender + 63) >> 6);
    const int descent = (int)((-m->descender + 63) >> 6);
    const int line_height = ascent + descent;
    const int baseline = pill_y + (PILL_H - line_height) / 2 + ascent;
    int pen = pill_x + (PILL_W - text_advance(bar->text_face, state->clock)) / 2;
    for (const char *c = state->clock; *c; ++c)
        pen += draw_glyph(bar->text_face, (unsigned char)*c, argb, width, stride_px, pen, baseline, 0xFFFFFFu);

    /* Wi-Fi bars */
    for (int i = 0; i < 4; ++i) {
        const int on = state->wifi_up && state->wifi_pct >= kBarThresholds[i];
        round_rect(argb, width, stride_px, wifi_x + i * BAR_PITCH, wifi_y + WIFI_H - kBarHeights[i], BAR_W, kBarHeights[i],
                   BAR_RADIUS, on ? BAR_ON : BAR_OFF, 255);
    }

    /* Bluetooth glyph: right edge at bt_right, same baseline as the clock text (an lv_label at the pill's row) */
    if (state->bt_on && bar->icon_face) {
        if (FT_Load_Char(bar->icon_face, BT_CODEPOINT, FT_LOAD_DEFAULT) == 0) {
            const int advance = (int)(bar->icon_face->glyph->advance.x >> 6);
            const int label_top = pill_y + BT_Y_OFFSET;
            draw_glyph(bar->icon_face, BT_CODEPOINT, argb, width, stride_px, bt_right - advance, label_top + ascent,
                       state->bt_connected ? BT_CONNECTED : BT_IDLE);
        }
    }
}
