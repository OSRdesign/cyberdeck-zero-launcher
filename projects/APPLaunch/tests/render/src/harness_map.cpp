/* SPDX-License-Identifier: MIT
 *
 * Render harness: the map widget of cp0_lvgl (include/cp0_map_widget.h, report 054 3.7, Deck Atlas 0.1 task 2) on a
 * page of its own, over synthetic tiles only. "mapdemo open" draws an imaginary town (streets every 250 m, main roads,
 * a river, a lake, a park, buildings near the centre, each tile's z/x/y in a corner) into z/x/y folders, imports them
 * into a store under out/<WxH>/runtime/maps with the real importer (folders converted into MBTiles), and shows the
 * widget between the page header and a bottom tab strip (the room an app such as Deck Atlas gives it):
 *   main   256 px tiles, zoom 8-14 around 48.60 N 2.00 E, three z14 tiles left out (the missing-tile placeholder)
 *   hd     512 px ("@2x") tiles, zoom 12-14 (drawn at 0.5x / 0.75x / 1x by the display scale)
 *   usb    256 px tiles, zoom 8-13, registered in place from a folder that "mapdemo usb out" moves away
 *   online, online-key   two online source records (task 3) whose viewing caches hold the main town's tiles; the store
 *          is opened with the network off, so the widget shows their state strip ("Offline: saved tiles only", and
 *          "This source needs an API key" for the one whose URL has {key}); nothing is ever fetched
 * The widget's tile-load thread is not one of the harness's tracked workers (it lives as long as the page): scenes call
 * "mapdemo wait" before a shot, which blocks until every tile of the view is in (cp0_map_wait_idle).
 */

#include "harness.h"

#include "cp0_map_widget.h"
#include "cp0_ui_metrics.h"
#include "cp0_ui_metrics_lvgl.h"
#include "cp0_ui_roller.h"
#include "cp0_ui_widgets.hpp"
#include "lvgl/lvgl.h"
#include "native_ui.hpp"

#include <png.h>
#include <sys/stat.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kLat0 = 48.60, kLon0 = 2.00, kBoxM = 5000.0;

struct MapDemo {
    lv_obj_t *screen = nullptr;
    std::unique_ptr<cp0::ui::Page> page;
    lv_obj_t *body = nullptr, *map = nullptr, *tabs = nullptr;
    lv_group_t *group = nullptr;
    cp0_map_store_t *store = nullptr;
    cp0_map_layer_t *markers = nullptr, *track = nullptr;
    std::string dir;
};
MapDemo g_map;

/* ---------------------------------------------------------------------------------------------- synthetic tiles */

double m_per_world0()
{
    return 2.0 * M_PI * 6378137.0 * std::cos(kLat0 * M_PI / 180.0) / 256.0;
}

/* a 3 x 5 pixel font for the z/x/y corner label */
const char *glyph(char c)
{
    static const char *digits[10] = {"111101101101111", "010110010010111", "111001111100111", "111001111001111",
                                     "101101111001001", "111100111001111", "111100111101111", "111001001001001",
                                     "111101111101111", "111101111001111"};
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == '/') return "001001010100100";
    return nullptr;
}

void put(std::vector<uint8_t> &rgb, int px, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x < 0 || y < 0 || x >= px || y >= px) return;
    uint8_t *p = &rgb[(static_cast<size_t>(y) * px + x) * 3];
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

void label(std::vector<uint8_t> &rgb, int px, const std::string &s)
{
    const int k = px / 128; /* 2 px per font pixel at 256, 4 at 512 */
    int x = 3 * k;
    for (char c : s) {
        const char *gl = glyph(c);
        if (gl)
            for (int j = 0; j < 5; j++)
                for (int i = 0; i < 3; i++)
                    if (gl[j * 3 + i] == '1')
                        for (int a = 0; a < k; a++)
                            for (int b = 0; b < k; b++) put(rgb, px, x + i * k + a, 3 * k + j * k + b, 90, 90, 90);
        x += 4 * k;
    }
}

void draw_tile(int z, uint32_t tx, uint32_t ty, int px, std::vector<uint8_t> &rgb)
{
    rgb.assign(static_cast<size_t>(px) * px * 3, 0);
    double ox0, oy0;
    cp0_map_latlon_to_world_px(kLat0, kLon0, 0, &ox0, &oy0);
    const double m0 = m_per_world0(), n = std::ldexp(1.0, z);
    const double mpp = m0 * 256.0 / (n * px); /* metres per tile pixel */
    const double hw_street = std::max(5.0, 0.7 * mpp), hw_main = std::max(11.0, 1.2 * mpp), hw_river = 55.0;
    for (int j = 0; j < px; j++)
        for (int i = 0; i < px; i++) {
            const double wx0 = (tx + (i + 0.5) / px) * 256.0 / n, wy0 = (ty + (j + 0.5) / px) * 256.0 / n;
            const double mx = (wx0 - ox0) * m0, my = -(wy0 - oy0) * m0; /* metres east / north of the centre */
            uint8_t r = 238, g = 234, b = 222;                         /* land */
            const double gx = std::fabs(std::remainder(mx, 250.0)), gy = std::fabs(std::remainder(my, 250.0));
            const double town = std::hypot(mx, my);
            if (mx > -2600 && mx < -1400 && my > 400 && my < 1600) r = 196, g = 222, b = 176; /* park */
            if (town < 3200 && gx > 40 && gy > 40 && gx < 110 && gy < 110) r = 214, g = 200, b = 188; /* buildings */
            if (gx < hw_street || gy < hw_street) r = g = b = 255;    /* streets */
            const double mgx = std::fabs(std::remainder(mx, 1000.0)), mgy = std::fabs(std::remainder(my, 1000.0));
            if (mgx < hw_main || mgy < hw_main) r = 248, g = 214, b = 120; /* main roads */
            const double river = my - 700.0 * std::sin(mx / 1300.0) + 900.0;
            if (std::fabs(river) < hw_river || std::hypot(mx - 1800, my + 2200) < 650) r = 160, g = 200, b = 232;
            put(rgb, px, i, j, r, g, b);
        }
    for (int k = 0; k < px; k++) { /* a faint tile edge, for review */
        put(rgb, px, k, 0, 205, 205, 205);
        put(rgb, px, 0, k, 205, 205, 205);
    }
    label(rgb, px, std::to_string(z) + "/" + std::to_string(tx) + "/" + std::to_string(ty));
}

bool write_png(const std::string &path, const std::vector<uint8_t> &rgb, int px)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(f);
        return false;
    }
    png_init_io(png, f);
    png_set_IHDR(png, info, px, px, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < px; y++) png_write_row(png, const_cast<png_bytep>(&rgb[static_cast<size_t>(y) * px * 3]));
    png_write_end(png, info);
    png_destroy_write_struct(&png, &info);
    return std::fclose(f) == 0;
}

void mkdirs(const std::string &path)
{
    for (size_t at = 1; (at = path.find('/', at)) != std::string::npos; ++at) ::mkdir(path.substr(0, at).c_str(), 0755);
    ::mkdir(path.c_str(), 0755);
}

/* a z/x/y folder of the box around the centre */
bool make_folder(const std::string &dir, int zmin, int zmax, int px, bool holes)
{
    std::vector<uint8_t> rgb;
    const double dlat = kBoxM / 111320.0, dlon = kBoxM / (111320.0 * std::cos(kLat0 * M_PI / 180.0));
    const cp0_map_bbox_t box = {kLon0 - dlon, kLat0 - dlat, kLon0 + dlon, kLat0 + dlat};
    uint32_t cx, cy;
    cp0_map_latlon_to_tile(kLat0, kLon0, 14, &cx, &cy);
    for (int z = zmin; z <= zmax; z++) {
        uint32_t x0, y0, x1, y1;
        cp0_map_bbox_tiles(&box, z, &x0, &y0, &x1, &y1);
        for (uint32_t x = x0; x <= x1; x++)
            for (uint32_t y = y0; y <= y1; y++) {
                if (holes && z == 14 && y == cy - 1 && (x == cx + 2 || x == cx + 3)) continue;
                if (holes && z == 14 && y == cy && x == cx + 3) continue;
                const std::string d = dir + "/" + std::to_string(z) + "/" + std::to_string(x);
                mkdirs(d);
                draw_tile(z, x, y, px, rgb);
                if (!write_png(d + "/" + std::to_string(y) + ".png", rgb, px)) return false;
            }
    }
    return true;
}

/* the online demo sources: a record each, and a viewing cache made from the converted main archive */
void online_sources(const std::string &store_dir)
{
    struct stat st;
    mkdirs(store_dir + "/sources");
    mkdirs(store_dir + "/tiles");
    static const struct {
        const char *id, *title, *url;
    } recs[] = {{"online", "Online demo", "http://127.0.0.1:1/{z}/{x}/{y}.png"},
                {"online-key", "Online demo with a key", "http://127.0.0.1:1/{z}/{x}/{y}.png?key={key}"}};
    for (const auto &r : recs) {
        const std::string conf = store_dir + "/sources/" + r.id + ".conf";
        if (FILE *f = std::fopen(conf.c_str(), "w")) {
            std::fprintf(f, "v=1\ntitle=%s\nurl=%s\nenabled=1\nminzoom=8\nmaxzoom=14\n"
                            "attribution=\xc2\xa9 OpenStreetMap contributors (synthetic online demo)\n",
                         r.title, r.url);
            std::fclose(f);
        }
        const std::string cache = store_dir + "/tiles/" + r.id + ".mbtiles";
        if (::stat(cache.c_str(), &st) == 0) continue;
        FILE *in = std::fopen((store_dir + "/archives/main.mbtiles").c_str(), "rb");
        FILE *out = in ? std::fopen(cache.c_str(), "wb") : nullptr;
        char buf[65536];
        size_t n;
        while (in && out && (n = std::fread(buf, 1, sizeof buf, in)) > 0) std::fwrite(buf, 1, n, out);
        if (out) std::fclose(out);
        if (in) std::fclose(in);
    }
}

bool open_store(const std::string &dir, std::string *err)
{
    struct stat st;
    /* a run that stopped with the stick "out": put it back */
    if (::stat((dir + "/usb/stick-out").c_str(), &st) == 0 && ::stat((dir + "/usb/stick").c_str(), &st) != 0)
        std::rename((dir + "/usb/stick-out").c_str(), (dir + "/usb/stick").c_str());
    const bool fresh = ::stat((dir + "/store/archives/main.conf").c_str(), &st) != 0;
    if (fresh) {
        if (!make_folder(dir + "/src/main", 8, 14, 256, true) || !make_folder(dir + "/src/hd", 12, 14, 512, false) ||
            !make_folder(dir + "/src/usb", 8, 13, 256, false)) {
            *err = "mapdemo: cannot write the synthetic tiles under " + dir;
            return false;
        }
        /* the USB stick's archive: the folder converted in a store of its own, the MBTiles moved onto the "stick" */
        cp0_map_store_cfg_t ucfg{};
        const std::string ustore = dir + "/usbconv";
        ucfg.store_dir = ustore.c_str();
        cp0_map_store_t *us = nullptr;
        cp0_map_import_opts_t uo{};
        uo.id = "region";
        uo.title = "USB stick town";
        int urc = cp0_map_store_open(&ucfg, &us);
        if (urc == CP0_MAP_OK) urc = cp0_map_store_import_ex(us, (dir + "/src/usb").c_str(), &uo);
        if (us) cp0_map_store_close(us);
        mkdirs(dir + "/usb/stick");
        if (urc != CP0_MAP_OK ||
            std::rename((ustore + "/archives/region.mbtiles").c_str(), (dir + "/usb/stick/region.mbtiles").c_str()) != 0) {
            *err = "mapdemo: cannot make the USB stick archive";
            return false;
        }
    }
    cp0_map_store_cfg_t cfg{};
    const std::string store_dir = dir + "/store";
    if (!fresh) online_sources(store_dir);
    cfg.store_dir = store_dir.c_str();
    cfg.ram_cache_bytes = 24u << 20; /* network off: online sources show their saved tiles only, nothing is fetched */
    int rc = cp0_map_store_open(&cfg, &g_map.store);
    if (rc) {
        *err = std::string("mapdemo: store: ") + cp0_map_strerror(rc);
        return false;
    }
    if (fresh) {
        const struct {
            const char *id, *path, *title;
            int copy;
        } imports[] = {{"main", "/src/main", "Synthetic town", 1}, {"hd", "/src/hd", "Synthetic town @2x", 1},
                       {"usb", "/usb/stick/region.mbtiles", "USB stick town", 0}};
        for (const auto &im : imports) {
            cp0_map_import_opts_t o{};
            o.copy = im.copy;
            o.id = im.id;
            o.title = im.title;
            o.attribution = "\xc2\xa9 OpenStreetMap contributors (synthetic test tiles)";
            if ((rc = cp0_map_store_import_ex(g_map.store, (dir + im.path).c_str(), &o)) != CP0_MAP_OK) {
                *err = std::string("mapdemo: import ") + im.id + ": " + cp0_map_strerror(rc);
                return false;
            }
        }
        online_sources(store_dir);
        if ((rc = cp0_map_store_refresh(g_map.store)) != CP0_MAP_OK) {
            *err = std::string("mapdemo: refresh: ") + cp0_map_strerror(rc);
            return false;
        }
    }
    return true;
}

void destroy()
{
    if (g_map.group) lv_group_delete(g_map.group);
    MapDemo old = std::move(g_map);
    g_map = MapDemo{};
    g_map.dir = old.dir;
    old.page.reset();
    if (old.screen && old.screen != lv_screen_active()) lv_obj_delete(old.screen);
    if (old.store) cp0_map_store_close(old.store);
}

bool open_page(const std::string &dir, std::string *err)
{
    MapDemo old = std::move(g_map);
    g_map = MapDemo{};
    g_map.dir = dir;
    if (!open_store(dir, err)) return false;
    native_ui::begin_page(true, false);
    MapDemo &d = g_map;
    d.screen = lv_obj_create(nullptr);
    lv_obj_remove_style_all(d.screen);
    lv_obj_set_style_bg_color(d.screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(d.screen, LV_OPA_COVER, 0);
    cp0_ui_roller_page_t layout{};
    cp0_ui_roller_page(cp0_ui_metrics_get(), cp0::ui::status_strip_left(), &layout);
    d.page = std::make_unique<cp0::ui::Page>(
        d.screen, layout, [](lv_obj_t *header, const char *text) { return cp0::ui::add_title(header, text); }, [] {});
    d.page->set_title({"Map demo"});
    /* the app's room for the map: under the header, over a tab strip one row high */
    const int tab_h = cp0_ui_tok(CP0_TOK_ROW_H);
    d.body = lv_obj_create(d.page->root());
    lv_obj_remove_style_all(d.body);
    lv_obj_set_pos(d.body, 0, layout.body_y);
    lv_obj_set_size(d.body, layout.w, layout.body_h - tab_h);
    lv_obj_remove_flag(d.body, LV_OBJ_FLAG_SCROLLABLE);
    d.tabs = lv_obj_create(d.page->root());
    lv_obj_remove_style_all(d.tabs);
    lv_obj_set_pos(d.tabs, 0, layout.body_y + layout.body_h - tab_h);
    lv_obj_set_size(d.tabs, layout.w, tab_h);
    lv_obj_set_style_bg_color(d.tabs, lv_color_hex(CP0_UI_ROLLER_BAR_RGB), 0);
    lv_obj_set_style_bg_opa(d.tabs, LV_OPA_COVER, 0);
    lv_obj_remove_flag(d.tabs, LV_OBJ_FLAG_SCROLLABLE);
    static const char *const names[3] = {"Map", "GPS", "Archives"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *t = lv_label_create(d.tabs);
        lv_label_set_text(t, names[i]);
        lv_obj_set_style_text_color(t, lv_color_hex(i == 0 ? 0xFFFFFF : 0x9A9A9A), 0);
        lv_obj_set_style_text_font(t, cp0_ui_font(CP0_TEXT_BODY), 0);
        lv_obj_align(t, LV_ALIGN_CENTER, layout.w * (2 * i + 1) / 6 - layout.w / 2, 0);
    }
    {
        harness::IgnoreNewThreads ignore; /* the tile-load thread lives as long as the page */
        d.map = cp0_map_create(d.body, d.store);
    }
    if (!d.map) {
        *err = "mapdemo: cp0_map_create failed";
        return false;
    }
    cp0_map_set_source(d.map, "main");
    cp0_map_set_zoom(d.map, 14);
    cp0_map_set_center(d.map, kLat0, kLon0);
    d.group = lv_group_create();
    lv_group_add_obj(d.group, d.map);
    lv_group_focus_obj(d.map);
    for (lv_indev_t *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) lv_indev_set_group(indev, d.group);
    lv_screen_load(d.screen);
    cp0::ui::StatusStrip::add(d.page->header(), native_ui::fill_status);
    /* the old page goes after the new one is shown */
    if (old.group) lv_group_delete(old.group);
    old.page.reset();
    if (old.screen) lv_obj_delete(old.screen);
    if (old.store) cp0_map_store_close(old.store);
    return true;
}

void demo_markers()
{
    if (!g_map.markers) g_map.markers = cp0_map_add_markers(g_map.map, "demo");
    const double dlat = 1.0 / 111320.0, dlon = 1.0 / (111320.0 * std::cos(kLat0 * M_PI / 180.0));
    const cp0_map_marker_t m[] = {
        {kLat0 + 420 * dlat, kLon0 - 650 * dlon, CP0_MAP_MARKER_NODE, "Base node"},
        {kLat0 + 380 * dlat, kLon0 - 560 * dlon, CP0_MAP_MARKER_NODE, "Rover"},
        {kLat0 - 310 * dlat, kLon0 + 520 * dlon, CP0_MAP_MARKER_REPEATER, "Hill repeater"},
        {kLat0 + 150 * dlat, kLon0 + 900 * dlon, CP0_MAP_MARKER_ROOM, "Town hall"},
        {kLat0 - 600 * dlat, kLon0 - 300 * dlon, CP0_MAP_MARKER_WAYPOINT, "Camp"},
        {kLat0 - 120 * dlat, kLon0 - 1100 * dlon, CP0_MAP_MARKER_GENERIC, "Spot"},
    };
    cp0_map_markers_set(g_map.markers, m, static_cast<int>(sizeof m / sizeof m[0]));
}

void demo_track()
{
    if (!g_map.track) g_map.track = cp0_map_add_track(g_map.map, "walk");
    cp0_map_track_clear(g_map.track);
    const double dlat = 1.0 / 111320.0, dlon = 1.0 / (111320.0 * std::cos(kLat0 * M_PI / 180.0));
    for (int i = 0; i <= 400; i++) {
        const double t = i / 400.0, a = t * 5.0 * M_PI;
        const double e = -1400 + 2600 * t + 120 * std::sin(a * 3), n = -700 + 500 * std::sin(a) + 40 * std::cos(a * 7);
        cp0_map_track_append(g_map.track, kLat0 + n * dlat, kLon0 + e * dlon);
    }
}

} // namespace

namespace harness {

void *map_demo_widget()
{
    return g_map.map;
}

bool map_demo(const std::vector<std::string> &w, const std::string &dir, std::string *err)
{
    const std::string sub = w.size() > 1 ? w[1] : "";
    if (sub == "open") {
        if (!open_page(dir, err)) return false;
        return true;
    }
    if (sub == "close") {
        destroy();
        return true;
    }
    if (!g_map.map) {
        *err = "mapdemo " + sub + " needs a map (mapdemo open first)";
        return false;
    }
    if (sub == "wait") {
        for (int i = 0; i < 3; i++)
            if (cp0_map_wait_idle(g_map.map, 20000) != CP0_MAP_OK) {
                *err = "mapdemo wait: tiles still loading after 20 s";
                return false;
            }
        return true;
    }
    if (sub == "source" && w.size() == 3) {
        const int rc = cp0_map_set_source(g_map.map, w[2] == "all" ? nullptr : w[2].c_str());
        if (rc) *err = "mapdemo source: " + std::string(cp0_map_strerror(rc));
        return rc == CP0_MAP_OK;
    }
    if (sub == "center" && w.size() == 4) {
        cp0_map_set_center(g_map.map, std::atof(w[2].c_str()), std::atof(w[3].c_str()));
        return true;
    }
    if (sub == "offset" && w.size() == 4) { /* metres east / north of the town centre */
        const double n = std::atof(w[3].c_str()), e = std::atof(w[2].c_str());
        cp0_map_set_center(g_map.map, kLat0 + n / 111320.0, kLon0 + e / (111320.0 * std::cos(kLat0 * M_PI / 180.0)));
        return true;
    }
    if (sub == "zoom" && w.size() == 3) {
        cp0_map_set_zoom(g_map.map, std::atoi(w[2].c_str()));
        return true;
    }
    if (sub == "scale" && w.size() == 3) {
        float s = 0;
        if (cp0_map_scale_parse(w[2].c_str(), &s) != CP0_MAP_OK) {
            *err = "mapdemo scale auto|1x|1.5x|2x";
            return false;
        }
        cp0_map_set_scale(g_map.map, s);
        return true;
    }
    if (sub == "fix") { /* fix off | fix EAST NORTH ACC SPEED COURSE (metres from the centre) */
        if (w.size() == 3 && w[2] == "off") {
            cp0_map_set_position(g_map.map, nullptr);
            return true;
        }
        if (w.size() != 7) {
            *err = "usage: mapdemo fix off | fix EAST NORTH ACC_M SPEED_MPS COURSE_DEG";
            return false;
        }
        cp0_map_fix_t f{};
        f.lat = kLat0 + std::atof(w[3].c_str()) / 111320.0;
        f.lon = kLon0 + std::atof(w[2].c_str()) / (111320.0 * std::cos(kLat0 * M_PI / 180.0));
        f.acc_m = static_cast<float>(std::atof(w[4].c_str()));
        f.speed_mps = static_cast<float>(std::atof(w[5].c_str()));
        f.course_deg = static_cast<float>(std::atof(w[6].c_str()));
        f.mono_ms = 0; /* always fresh: the harness's clock is not the monotonic one */
        cp0_map_set_position(g_map.map, &f);
        return true;
    }
    if (sub == "markers") {
        demo_markers();
        if (w.size() == 3) cp0_map_markers_select(g_map.markers, std::atoi(w[2].c_str()));
        return true;
    }
    if (sub == "track") {
        demo_track();
        return true;
    }
    if (sub == "usb" && w.size() == 3 && (w[2] == "out" || w[2] == "in")) {
        const std::string in = dir + "/usb/stick", out = dir + "/usb/stick-out";
        if (std::rename(w[2] == "out" ? in.c_str() : out.c_str(), w[2] == "out" ? out.c_str() : in.c_str()) != 0) {
            *err = "mapdemo usb: rename failed";
            return false;
        }
        return true;
    }
    if (sub == "units" && w.size() == 3) {
        cp0_map_set_units(g_map.map, w[2] == "imperial" ? CP0_MAP_UNITS_IMPERIAL : CP0_MAP_UNITS_METRIC);
        return true;
    }
    *err = "usage: mapdemo open|close|wait|source ID|all|center LAT LON|offset E N|zoom Z|scale S|fix ...|markers [SEL]|"
           "track|usb out|in|units metric|imperial";
    return false;
}

} // namespace harness
