/*
 * Unit test of the app contract's per-launch environment (main/ui/app_launch_env.cpp, task 014 P5): the variables
 * for a compat and a fullscreen app on the deck, the Pi 3A+ and the larger screens (from the real layout service),
 * shell quoting, and what a shell actually exports, read back with the contract's own reader.
 */
#include "app_launch_env.hpp"

#include "cp0_app_contract.h"
#include "cp0_ui_metrics.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int failures;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                              \
        }                                                                            \
    } while (0)

static cp0_ui_metrics_t compute(int w, int h, int ppmm)
{
    cp0_ui_metrics_t m{};
    CHECK(cp0_ui_metrics_compute(w, h, 0, ppmm, ppmm > 0 ? CP0_UI_DENSITY_PANEL_MM : CP0_UI_DENSITY_DEFAULT, 0,
                                 &m) == 0);
    return m;
}

static std::string value_of(const app_launch_env::Vars &vars, const std::string &name)
{
    for (const auto &kv : vars)
        if (kv.first == name) return kv.second;
    return "<absent>";
}

static void test_compat_deck()
{
    const cp0_ui_metrics_t m = compute(640, 480, 0);
    app_launch_env::Input in;
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, "/run/user/1000", &in));
    const app_launch_env::Vars vars = app_launch_env::variables(in);
    CHECK(vars.size() == 6);
    CHECK(vars[0].first == "APPLAUNCH_CONTRACT" && vars[0].second == "1");
    CHECK(value_of(vars, "APPLAUNCH_APP_MODE") == "compat");
    CHECK(value_of(vars, "APPLAUNCH_WINDOW") == "0,0,640,340");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS") == "320x170");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS_SCALE") == "2");
    CHECK(value_of(vars, "APPLAUNCH_STATE_DIR") == "/run/user/1000/applaunch");
    CHECK(app_launch_env::export_command(in) ==
          "export APPLAUNCH_CONTRACT='1' APPLAUNCH_APP_MODE='compat' APPLAUNCH_WINDOW='0,0,640,340' "
          "APPLAUNCH_CANVAS='320x170' APPLAUNCH_CANVAS_SCALE='2' APPLAUNCH_STATE_DIR='/run/user/1000/applaunch'; ");
}

static void test_compat_and_fullscreen_sizes()
{
    /* the Pi 3A+: 1x window at (80,25) */
    cp0_ui_metrics_t m = compute(480, 320, 630);
    app_launch_env::Input in;
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, "/run/user/1000", &in));
    app_launch_env::Vars vars = app_launch_env::variables(in);
    CHECK(value_of(vars, "APPLAUNCH_WINDOW") == "80,25,320,170");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS") == "320x170");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS_SCALE") == "1");
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Fullscreen, "/run/user/1000", &in));
    vars = app_launch_env::variables(in);
    CHECK(value_of(vars, "APPLAUNCH_APP_MODE") == "fullscreen");
    CHECK(value_of(vars, "APPLAUNCH_WINDOW") == "0,0,480,320");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS") == "480x320");
    CHECK(value_of(vars, "APPLAUNCH_CANVAS_SCALE") == "1");

    /* the window is the layout service's compat placement at every size (800x480: 2x at x 80; 1280x720: 3x) */
    static const struct {
        int w, h, ppmm;
    } sizes[] = {{800, 480, 920}, {720, 720, 1000}, {1280, 720, 1160}, {1024, 600, 660}};
    for (const auto &s : sizes) {
        m = compute(s.w, s.h, s.ppmm);
        CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, nullptr, &in));
        vars = app_launch_env::variables(in);
        const std::string window = std::to_string(m.compat.x) + "," + std::to_string(m.compat.y) + "," +
                                   std::to_string(m.compat.w) + "," + std::to_string(m.compat.h);
        CHECK(value_of(vars, "APPLAUNCH_WINDOW") == window);
        CHECK(value_of(vars, "APPLAUNCH_CANVAS_SCALE") == std::to_string(m.compat.scale));
        CHECK(m.compat.w == 320 * m.compat.scale && m.compat.h == 170 * m.compat.scale);
        CHECK(value_of(vars, "APPLAUNCH_STATE_DIR") == "<absent>"); /* no XDG_RUNTIME_DIR: not exported */
        CHECK(vars.size() == 5);
        CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Fullscreen, "", &in));
        vars = app_launch_env::variables(in);
        CHECK(value_of(vars, "APPLAUNCH_CANVAS") == std::to_string(s.w) + "x" + std::to_string(s.h));
        CHECK(value_of(vars, "APPLAUNCH_WINDOW") == "0,0," + std::to_string(s.w) + "," + std::to_string(s.h));
    }
    m = compute(800, 480, 920);
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, nullptr, &in));
    CHECK(value_of(app_launch_env::variables(in), "APPLAUNCH_WINDOW") == "80,18,640,340");
    m = compute(1280, 720, 1160);
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, nullptr, &in));
    CHECK(value_of(app_launch_env::variables(in), "APPLAUNCH_WINDOW") == "160,43,960,510");
    CHECK(value_of(app_launch_env::variables(in), "APPLAUNCH_CANVAS_SCALE") == "3");
}

static void test_no_metrics()
{
    app_launch_env::Input in;
    in.canvas_w = 7;
    CHECK(!app_launch_env::from_metrics(nullptr, app_launch_env::Mode::Compat, "/run/user/1000", &in));
    CHECK(in.canvas_w == 7); /* untouched */
    /* the stock build / SDL: no current metrics, so the command is left as it was */
    cp0_ui_metrics_set_current(nullptr);
    CHECK(app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen).empty());
    const cp0_ui_metrics_t m = compute(640, 480, 0);
    cp0_ui_metrics_set_current(&m);
    setenv("XDG_RUNTIME_DIR", "/run/user/1000", 1);
    CHECK(app_launch_env::export_for_current(app_launch_env::Mode::Fullscreen) ==
          "export APPLAUNCH_CONTRACT='1' APPLAUNCH_APP_MODE='fullscreen' APPLAUNCH_WINDOW='0,0,640,480' "
          "APPLAUNCH_CANVAS='640x480' APPLAUNCH_CANVAS_SCALE='1' APPLAUNCH_STATE_DIR='/run/user/1000/applaunch'; ");
    cp0_ui_metrics_set_current(nullptr);
}

static void test_quoting_through_a_shell()
{
    CHECK(app_launch_env::shell_quote("") == "''");
    CHECK(app_launch_env::shell_quote("a b") == "'a b'");
    CHECK(app_launch_env::shell_quote("it's") == "'it'\\''s'");

    /* what sh really exports in front of the app's own Exec line, read with the contract's reader */
    const cp0_ui_metrics_t m = compute(720, 720, 1000);
    app_launch_env::Input in;
    CHECK(app_launch_env::from_metrics(&m, app_launch_env::Mode::Compat, "/run/user/1000 it's", &in));
    const std::string command = app_launch_env::export_command(in) +
                                "printf '%s|%s|%s|%s|%s|%s' \"$APPLAUNCH_CONTRACT\" \"$APPLAUNCH_APP_MODE\" "
                                "\"$APPLAUNCH_WINDOW\" \"$APPLAUNCH_CANVAS\" \"$APPLAUNCH_CANVAS_SCALE\" "
                                "\"$APPLAUNCH_STATE_DIR\"";
    std::string script = "sh -c " + app_launch_env::shell_quote(command);
    FILE *p = popen(script.c_str(), "r");
    CHECK(p != nullptr);
    char out[512] = {0};
    if (p) {
        const size_t n = std::fread(out, 1, sizeof(out) - 1, p);
        out[n] = '\0';
        pclose(p);
    }
    const std::string want = "1|compat|" + std::to_string(m.compat.x) + "," + std::to_string(m.compat.y) + "," +
                             std::to_string(m.compat.w) + "," + std::to_string(m.compat.h) + "|320x170|" +
                             std::to_string(m.compat.scale) + "|/run/user/1000 it's/applaunch";
    if (want != out) std::fprintf(stderr, "shell exported '%s', expected '%s'\n", out, want.c_str());
    CHECK(want == out);

    /* and the contract reader accepts exactly these variables */
    for (const auto &kv : app_launch_env::variables(in)) setenv(kv.first.c_str(), kv.second.c_str(), 1);
    cp0_app_env_t env;
    CHECK(cp0_app_env_read(&env) == 0);
    CHECK(env.contract == 1 && env.mode == CP0_APP_MODE_COMPAT);
    CHECK(env.win_x == m.compat.x && env.win_y == m.compat.y && env.win_w == m.compat.w && env.win_h == m.compat.h);
    CHECK(env.canvas_w == 320 && env.canvas_h == 170 && env.canvas_scale == m.compat.scale);
    CHECK(std::strcmp(env.state_dir, "/run/user/1000 it's/applaunch") == 0);
}

int main()
{
    test_compat_deck();
    test_compat_and_fullscreen_sizes();
    test_no_metrics();
    test_quoting_through_a_shell();
    if (failures) {
        std::fprintf(stderr, "test_app_launch_env: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_app_launch_env: PASS\n");
    return 0;
}
