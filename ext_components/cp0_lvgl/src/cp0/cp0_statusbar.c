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
/* ---- the battery (decision X12): fits in the 320 px strip left of the Bluetooth glyph's place, "100%" included */
#define BATT_GAP 12             /* between the battery and the Bluetooth glyph's place */
#define BATT_TEXT_GAP 3         /* between the percentage and the level glyph */
#define BATT_TEXT_PX 18         /* the percentage: the caption size beside the 28 px clock */
#define BATT_COLOR 0xFFFFFFu    /* the clock's text; BAR_ON while charging */
/* ---- the SSH indicator (2026-10-30): the leftmost slot, colours of the strip's own (no new colour) */
#define SSH_CODEPOINT 0xF120    /* Font Awesome 5 Solid "terminal" (">_"), in the icon font the strip loads */
#define SSH_GAP BATT_GAP        /* between it and the battery's level glyph, or the Bluetooth glyph's place */
#define SSH_IDLE BT_CONNECTED   /* a session: the strip's "connected" blue */
#define SSH_BRIGHT BATT_COLOR   /* traffic: the blink's other phase, the clock's white */

static const int kBarHeights[4] = {9, 15, 21, 28};
static const int kBarThresholds[4] = {1, 30, 60, 80};
/* LV_SYMBOL_BATTERY_FULL, _3, _2, _1, _EMPTY: the level glyph from this charge in percent up */
static const unsigned long kBatteryGlyphs[5] = {0xF240, 0xF241, 0xF242, 0xF243, 0xF244};
static const int kBatteryGlyphFrom[5] = {88, 63, 38, 13, 0};

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

/* path = $APPLAUNCH_STATUS_SYSFS_ROOT (test hook: a fake tree) + rel; rel when the variable is unset */
static const char *rooted(const char *rel, char *buf, size_t size)
{
    const char *root = getenv("APPLAUNCH_STATUS_SYSFS_ROOT");
    if (!root || !root[0]) return rel;
    snprintf(buf, size, "%s%s", root, rel);
    return buf;
}

static int iface_is_up(const char *name)
{
    char rel[160], path[480], line[32];
    snprintf(rel, sizeof(rel), "/sys/class/net/%.100s/operstate", name);
    return read_first_line(rooted(rel, path, sizeof(path)), line, sizeof(line)) && strncmp(line, "up", 2) == 0;
}

/* The Wi-Fi interface, whatever the kernel named it (wlan0, wlp2s0, wlx...): the entries of /proc/net/wireless (it lists
 * wireless interfaces only), else the /sys/class/net/<if>/wireless directories; the first one whose operstate is up.
 * The signal comes from its /proc/net/wireless line (0 when it has none). */
static void read_wifi(cp0_statusbar_state_t *state)
{
    char line[160], path[480];
    FILE *file = fopen(rooted("/proc/net/wireless", path, sizeof(path)), "r");
    while (file && fgets(line, sizeof(line), file)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;                                             /* the two header lines have none */
        *colon = '\0';
        const char *name = line;
        while (*name == ' ') ++name;
        float link = 0, level = 0;
        if (!*name || !iface_is_up(name)) continue;
        state->wifi_up = 1;
        if (sscanf(colon + 1, "%*s %f %f", &link, &level) == 2) {
            int pct = (int)(level < 0 ? 2 * (level + 100) : level);       /* dBm -> percent, as native_ui does */
            state->wifi_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        }
        break;
    }
    if (file) fclose(file);
    if (state->wifi_up) return;
    DIR *dir = opendir(rooted("/sys/class/net", path, sizeof(path)));
    struct dirent *entry;
    while (dir && (entry = readdir(dir))) {
        char rel[160], probe[480];
        if (entry->d_name[0] == '.') continue;
        snprintf(rel, sizeof(rel), "/sys/class/net/%.100s/wireless", entry->d_name);
        DIR *wireless = opendir(rooted(rel, probe, sizeof(probe)));
        if (!wireless) continue;
        closedir(wireless);
        if (iface_is_up(entry->d_name)) {
            state->wifi_up = 1;
            break;
        }
    }
    if (dir) closedir(dir);
}

void cp0_statusbar_read_state(cp0_statusbar_state_t *state)
{
    memset(state, 0, sizeof(*state));
    char path[480];
    read_wifi(state);
    DIR *dir = opendir(rooted("/sys/class/bluetooth", path, sizeof(path)));
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
        DIR *rf = opendir(rooted("/sys/class/rfkill", path, sizeof(path)));
        struct dirent *entry;
        while (rf && (entry = readdir(rf))) {
            char rel[320], full[480], kind[32] = "", soft[8] = "1";
            snprintf(rel, sizeof(rel), "/sys/class/rfkill/%.200s/type", entry->d_name);
            if (!read_first_line(rooted(rel, full, sizeof(full)), kind, sizeof(kind)) ||
                strncmp(kind, "bluetooth", 9) != 0)
                continue;
            snprintf(rel, sizeof(rel), "/sys/class/rfkill/%.200s/soft", entry->d_name);
            if (read_first_line(rooted(rel, full, sizeof(full)), soft, sizeof(soft)) && soft[0] != '0') state->bt_on = 0;
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

/* Where the battery goes: [percentage][level glyph], right to left from the Bluetooth glyph's place. */
typedef struct {
    unsigned long glyph; /* the level glyph */
    int glyph_x;         /* its pen x */
    char text[16];       /* "85%" */
    int text_x;          /* the percentage's pen x: the battery's left edge */
} battery_place_t;

static unsigned long battery_glyph(int percent)
{
    for (int i = 0; i < 4; ++i)
        if (percent >= kBatteryGlyphFrom[i]) return kBatteryGlyphs[i];
    return kBatteryGlyphs[4];
}

/* The icon face at the strip's size; the text face measured at text_px, then set back to font_px. */
static int battery_place(cp0_statusbar_t *bar, const cp0_statusbar_state_t *state, int bt_right, int gap, int text_gap,
                         int text_px, int font_px, battery_place_t *out)
{
    if (!bar->icon_face || FT_Load_Char(bar->icon_face, BT_CODEPOINT, FT_LOAD_DEFAULT) != 0) return 0;
    const int bt_left = bt_right - (int)(bar->icon_face->glyph->advance.x >> 6);
    const int percent = state->battery_pct < 0 ? 0 : state->battery_pct > 100 ? 100 : state->battery_pct;
    out->glyph = battery_glyph(percent);
    if (FT_Load_Char(bar->icon_face, out->glyph, FT_LOAD_DEFAULT) != 0) return 0;
    out->glyph_x = bt_left - gap - (int)(bar->icon_face->glyph->advance.x >> 6);
    snprintf(out->text, sizeof(out->text), "%d%%", percent);
    FT_Set_Pixel_Sizes(bar->text_face, 0, text_px);
    out->text_x = out->glyph_x - text_gap - text_advance(bar->text_face, out->text);
    FT_Set_Pixel_Sizes(bar->text_face, 0, font_px);
    return 1;
}

/* The advance of an icon glyph at the face's current size, -1 when the face cannot load it. */
static int icon_advance(cp0_statusbar_t *bar, unsigned long codepoint)
{
    if (!bar->icon_face || FT_Load_Char(bar->icon_face, codepoint, FT_LOAD_DEFAULT) != 0) return -1;
    return (int)(bar->icon_face->glyph->advance.x >> 6);
}

/* Scales a design pixel value by pct percent (exact at 100). */
#define SC(v) (((v) * pct + 50) / 100)

void cp0_statusbar_render_scaled(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                                 int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state,
                                 int pct)
{
    if (!bar || !argb || !state) return;
    if (pct < 25 || pct > 100) pct = 100;
    if (pct != 100) {
        FT_Set_Pixel_Sizes(bar->text_face, 0, SC(FONT_PX));
        if (bar->icon_face) FT_Set_Pixel_Sizes(bar->icon_face, 0, SC(FONT_PX));
    }
    const int pill_w = SC(PILL_W), pill_h = SC(PILL_H);
    const int pill_x = width - SC(EDGE) - pill_w - shift_left;
    const int pill_y = top;
    const int wifi_x = pill_x - SC(WIFI_GAP) - SC(WIFI_W);
    const int wifi_y = pill_y + SC(WIFI_Y_OFFSET);
    const int bt_right = wifi_x - SC(BT_GAP);
    battery_place_t battery = {0};
    const int has_battery = state->battery && battery_place(bar, state, bt_right, SC(BATT_GAP), SC(BATT_TEXT_GAP),
                                                            SC(BATT_TEXT_PX), SC(FONT_PX), &battery);
    /* the SSH indicator: the leftmost slot, its right edge SSH_GAP left of the battery's level glyph (the percentage is
     * left out while it shows), else of the Bluetooth glyph's place (drawn or not) */
    int ssh_x = 0;
    int has_ssh = 0;
    if (state->ssh > 0) {
        const int bt_advance = icon_advance(bar, BT_CODEPOINT);
        const int ssh_advance = icon_advance(bar, SSH_CODEPOINT);
        if (bt_advance >= 0 && ssh_advance >= 0) {
            const int right = has_battery ? battery.glyph_x : bt_right - bt_advance;
            ssh_x = right - SC(SSH_GAP) - ssh_advance;
            has_ssh = 1;
        }
    }
    const int battery_text = has_battery && !has_ssh;

    if (backing_alpha > 0) {
        int left = bt_right - SC(34);
        if (battery_text && battery.text_x - SC(6) < left) left = battery.text_x - SC(6);
        if (has_ssh && ssh_x - SC(6) < left) left = ssh_x - SC(6);
        round_rect(argb, width, stride_px, left, pill_y - SC(2), pill_x + pill_w + SC(6) - left, pill_h + SC(4), SC(12),
                   0x000000u, backing_alpha);
    }

    /* clock pill with the time centred like an lv_label (line height = ascender - descender) */
    round_rect(argb, width, stride_px, pill_x, pill_y, pill_w, pill_h, SC(PILL_RADIUS), PILL_COLOR, 255);
    const FT_Size_Metrics *m = &bar->text_face->size->metrics;
    const int ascent = (int)((m->ascender + 63) >> 6);
    const int descent = (int)((-m->descender + 63) >> 6);
    const int line_height = ascent + descent;
    const int baseline = pill_y + (pill_h - line_height) / 2 + ascent;
    int pen = pill_x + (pill_w - text_advance(bar->text_face, state->clock)) / 2;
    for (const char *c = state->clock; *c; ++c)
        pen += draw_glyph(bar->text_face, (unsigned char)*c, argb, width, stride_px, pen, baseline, 0xFFFFFFu);

    /* Wi-Fi bars */
    for (int i = 0; i < 4; ++i) {
        const int on = state->wifi_up && state->wifi_pct >= kBarThresholds[i];
        round_rect(argb, width, stride_px, wifi_x + i * SC(BAR_PITCH), wifi_y + SC(WIFI_H) - SC(kBarHeights[i]), SC(BAR_W),
                   SC(kBarHeights[i]), SC(BAR_RADIUS), on ? BAR_ON : BAR_OFF, 255);
    }

    /* Bluetooth glyph: right edge at bt_right, same baseline as the clock text (an lv_label at the pill's row) */
    if (state->bt_on && bar->icon_face) {
        if (FT_Load_Char(bar->icon_face, BT_CODEPOINT, FT_LOAD_DEFAULT) == 0) {
            const int advance = (int)(bar->icon_face->glyph->advance.x >> 6);
            const int label_top = pill_y + SC(BT_Y_OFFSET);
            draw_glyph(bar->icon_face, BT_CODEPOINT, argb, width, stride_px, bt_right - advance, label_top + ascent,
                       state->bt_connected ? BT_CONNECTED : BT_IDLE);
        }
    }

    /* Battery: the level glyph on the Bluetooth glyph's baseline, the percentage's digits centred on the glyph */
    if (has_battery) {
        const uint32_t rgb = state->battery_charging ? BAR_ON : BATT_COLOR;
        const int icon_baseline = pill_y + SC(BT_Y_OFFSET) + ascent;
        int middle = icon_baseline - SC(11);
        if (FT_Load_Char(bar->icon_face, battery.glyph, FT_LOAD_DEFAULT) == 0) {
            const FT_Glyph_Metrics *gm = &bar->icon_face->glyph->metrics;
            middle = icon_baseline - (int)((gm->horiBearingY - gm->height / 2 + 32) >> 6);
        }
        draw_glyph(bar->icon_face, battery.glyph, argb, width, stride_px, battery.glyph_x, icon_baseline, rgb);
        if (battery_text) {
            FT_Set_Pixel_Sizes(bar->text_face, 0, SC(BATT_TEXT_PX));
            int digit_h = SC(13);
            if (FT_Load_Char(bar->text_face, '0', FT_LOAD_DEFAULT) == 0)
                digit_h = (int)((bar->text_face->glyph->metrics.horiBearingY + 32) >> 6);
            int x = battery.text_x;
            for (const char *c = battery.text; *c; ++c)
                x += draw_glyph(bar->text_face, (unsigned char)*c, argb, width, stride_px, x, middle + digit_h / 2, rgb);
            FT_Set_Pixel_Sizes(bar->text_face, 0, SC(FONT_PX));
        }
    }

    /* SSH indicator: the terminal glyph on the Bluetooth glyph's baseline */
    if (has_ssh)
        draw_glyph(bar->icon_face, SSH_CODEPOINT, argb, width, stride_px, ssh_x, pill_y + SC(BT_Y_OFFSET) + ascent,
                   state->ssh_bright ? SSH_BRIGHT : SSH_IDLE);
    if (pct != 100) {
        FT_Set_Pixel_Sizes(bar->text_face, 0, FONT_PX);
        if (bar->icon_face) FT_Set_Pixel_Sizes(bar->icon_face, 0, FONT_PX);
    }
}

void cp0_statusbar_render(cp0_statusbar_t *bar, uint32_t *argb, int width, int stride_px,
                          int shift_left, int top, int backing_alpha, const cp0_statusbar_state_t *state)
{
    cp0_statusbar_render_scaled(bar, argb, width, stride_px, shift_left, top, backing_alpha, state, 100);
}
