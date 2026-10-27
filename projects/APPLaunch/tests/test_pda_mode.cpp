/*
 * Unit test of Settings > Screen > PDA mode (main/ui/pda_mode.cpp, report 043 phase S7b): the canvases of Off / On
 * rotate left / On rotate right from the board profile, where the row is offered (the real layout service and writing
 * zone), the board's default, the stored value, the environment set at start, and apply_at_start() itself against a
 * config.json in a temporary HOME (in child processes: it runs once per process).
 */
#include "pda_mode.hpp"

#include "app_launch_env.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                                   \
        }                                                                                 \
    } while (0)

using pda_mode::Board;
using pda_mode::Canvas;

static bool same(const Canvas &c, int w, int h, int rot)
{
    return c.w == w && c.h == h && c.rot == rot;
}

static std::string value_of(const pda_mode::Vars &vars, const std::string &name)
{
    for (const auto &kv : vars)
        if (kv.first == name) return kv.second;
    return "<absent>";
}

static bool always(const Canvas &, const char *)
{
    return true;
}

static bool never(const Canvas &, const char *)
{
    return false;
}

static Canvas s_asked;
static bool record(const Canvas &c, const char *)
{
    s_asked = c;
    return true;
}

static void test_board_from()
{
    // the board profile's geometry, both variables: the deck has none (its unit sets APPLAUNCH_ROTATE=0 only)
    CHECK(!pda_mode::board_from(nullptr, "0", nullptr).known);
    CHECK(!pda_mode::board_from("640x480", nullptr, nullptr).known);
    CHECK(!pda_mode::board_from("", "0", nullptr).known);
    for (const char *bad : {"800x", "x480", "800*480", "0x480", "800x480x2", "80000x480", "-800x480", "800 x480"})
        CHECK(!pda_mode::board_from(bad, "0", nullptr).known);
    for (const char *bad : {"45", "-90", "090", "360", "", "ninety"})
        CHECK(!pda_mode::board_from("800x480", bad, nullptr).known);

    // the Pi 3A+ / Pi 4 B DSI landscape profile (800x480 framebuffer, rotate 0)
    Board b = pda_mode::board_from("800x480", "0", nullptr);
    CHECK(b.known && !b.pda && same(b.canvas, 800, 480, 0));
    CHECK(same(b.landscape, 800, 480, 0));
    CHECK(same(b.left, 480, 800, 90));   // the install profile's PDA canvas: the right edge at the top
    CHECK(same(b.right, 480, 800, 270)); // the other side: the left edge at the top

    // the PDA install profile pi3a-wsdsi43-portrait (the same panel turned): Off is the landscape profile's canvas
    b = pda_mode::board_from("480x800", "90", "1");
    CHECK(b.known && b.pda && same(b.canvas, 480, 800, 90));
    CHECK(same(b.landscape, 800, 480, 0));
    CHECK(same(b.left, 480, 800, 90) && b.left == b.canvas);
    CHECK(same(b.right, 480, 800, 270));
    // a board.conf edited by hand to the other side (rotate 270): the same landscape, the canvas is the right side
    b = pda_mode::board_from("480x800", "270", "1");
    CHECK(same(b.landscape, 800, 480, 0) && b.right == b.canvas && same(b.left, 480, 800, 90));

    // the FZ-M1 tablet (pc profile: the DRM framebuffer 1280x800, rotate 0)
    b = pda_mode::board_from("1280x800", "0", nullptr);
    CHECK(same(b.landscape, 1280, 800, 0) && same(b.left, 800, 1280, 90) && same(b.right, 800, 1280, 270));

    // the HyperPixel 4 (a portrait 480x800 framebuffer shown in landscape at rotate 90): left 180, right 0 (its own)
    b = pda_mode::board_from("800x480", "90", nullptr);
    CHECK(same(b.landscape, 800, 480, 90) && same(b.left, 480, 800, 180) && same(b.right, 480, 800, 0));
    // a portrait canvas at rotate 0 on such a framebuffer: its landscape is rotate 90 (the HyperPixel profile's)
    b = pda_mode::board_from("480x800", "0", "1");
    CHECK(same(b.landscape, 800, 480, 90) && b.right == b.canvas && same(b.left, 480, 800, 180));

    // a square panel (the HackBerry 720x720): no portrait, every canvas is the panel's
    b = pda_mode::board_from("720x720", "0", nullptr);
    CHECK(b.known && b.landscape == b.canvas && b.left == b.canvas && b.right == b.canvas);

    // every side: a landscape app run turned (a quarter turn back, the device turned clockwise) draws at the landscape
    // canvas' rotation on the left side and at its rotation + 180 on the right side
    for (const char *rot : {"0", "90", "180", "270"}) {
        for (const char *size : {"800x480", "480x800", "1280x800"}) {
            const Board t = pda_mode::board_from(size, rot, "1");
            CHECK(app_launch_env::turned_rotation(t.left.rot) == t.landscape.rot);
            CHECK(app_launch_env::turned_rotation(t.right.rot) == (t.landscape.rot + 180) % 360);
            CHECK(t.left.w == t.right.w && t.left.h == t.right.h && t.left.h > t.left.w);
        }
    }
}

static void test_offered()
{
    // the real rule: the board known, not square, the short side 480 px or more, the writing zone on the portrait
    // canvas
    const struct {
        const char *logical, *rotate, *panel_mm;
        bool offered;
        const char *why;
    } cases[] = {
        {"800x480", "0", "94x56", true, "Pi 3A+ 4.3\" DSI"},
        {"480x800", "90", "94x56", true, "the PDA install profile"},
        {"800x480", "0", "108x65", true, "Pi 4 B 5\" DSI"},
        {"1280x800", "0", "150x93", true, "FZ-M1 tablet"},
        {"1280x800", "0", nullptr, true, "the tablet at the default density"},
        {"800x480", "90", nullptr, true, "HyperPixel 4 (no panel size: the default density)"},
        {"1024x600", "0", "154x90", true, "a 7\" 1024x600"},
        {"640x480", "0", "71x53", true, "a 3.5\" 640x480 panel (480x640 portrait, report 041)"},
        {"720x720", "0", nullptr, false, "square"},
        {"480x320", "90", "79x49", false, "short side 320"},
        {"800x479", "0", nullptr, false, "short side 479"},
        {nullptr, "0", nullptr, false, "the deck: no board profile (its unit sets the rotation only)"},
    };
    for (const auto &c : cases) {
        const Board b = pda_mode::board_from(c.logical, c.rotate, nullptr);
        const bool got = pda_mode::offered(b, c.panel_mm);
        if (got != c.offered) std::fprintf(stderr, "offered(%s): %d, expected %d\n", c.why, got, c.offered);
        CHECK(got == c.offered);
        CHECK(pda_mode::choices(b, got).size() == (got ? 3u : 0u));
    }
    // the zone check gets the portrait canvas (the left one: both sides have its size) and decides
    const Board tablet = pda_mode::board_from("1280x800", "0", nullptr);
    CHECK(pda_mode::offered(tablet, nullptr, always));
    CHECK(!pda_mode::offered(tablet, nullptr, never));
    CHECK(pda_mode::offered(tablet, "150x93", record) && same(s_asked, 800, 1280, 90));
    CHECK(!pda_mode::offered(tablet, nullptr, nullptr));
    // the zone check: a portrait canvas the zone fits; never a landscape or square one
    CHECK(pda_mode::zone_fits(Canvas{480, 800, 90}, "94x56"));
    CHECK(pda_mode::zone_fits(Canvas{480, 640, 90}, "71x53"));
    CHECK(pda_mode::zone_fits(Canvas{800, 1280, 270}, "150x93"));
    CHECK(!pda_mode::zone_fits(Canvas{800, 480, 0}, "94x56"));
    CHECK(!pda_mode::zone_fits(Canvas{720, 720, 0}, nullptr));
    CHECK(!pda_mode::zone_fits(Canvas{320, 480, 90}, "79x49")); // 320 px wide: too little height for the zone's cap
    // what Settings lists, in its order
    const std::vector<int> values = pda_mode::choices(tablet, true);
    CHECK(values.size() == 3 && values[0] == pda_mode::kOff && values[1] == pda_mode::kLeft &&
          values[2] == pda_mode::kRight);
    CHECK(std::strcmp(pda_mode::label(pda_mode::kOff), "Off") == 0);
    CHECK(std::strcmp(pda_mode::label(pda_mode::kLeft), "On rotate left") == 0);
    CHECK(std::strcmp(pda_mode::label(pda_mode::kRight), "On rotate right") == 0);
    CHECK(pda_mode::choices(pda_mode::board_from(nullptr, nullptr, nullptr), true).empty());
}

static void test_board_default()
{
    // nothing stored: what board.conf says (the PDA install profile: On, its side; any landscape profile: Off)
    CHECK(pda_mode::board_default(pda_mode::board_from("480x800", "90", "1")) == pda_mode::kLeft);
    CHECK(pda_mode::board_default(pda_mode::board_from("480x800", "270", "1")) == pda_mode::kRight);
    CHECK(pda_mode::board_default(pda_mode::board_from("480x800", "90", "0")) == pda_mode::kOff);
    CHECK(pda_mode::board_default(pda_mode::board_from("480x800", "90", nullptr)) == pda_mode::kOff);
    CHECK(pda_mode::board_default(pda_mode::board_from("800x480", "0", "1")) == pda_mode::kOff);
    CHECK(pda_mode::board_default(pda_mode::board_from("480x800", nullptr, "1")) == pda_mode::kOff);
}

static void test_stored()
{
    CHECK(pda_mode::stored_from_json("{}") == -1);
    CHECK(pda_mode::stored_from_json("") == -1);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": 0}") == pda_mode::kOff);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": 1}") == pda_mode::kLeft);
    CHECK(pda_mode::stored_from_json("{\"dark_time\": 60, \"pda_mode\": 2, \"osk_mode\": 3}") == pda_mode::kRight);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": \"1\"}") == pda_mode::kLeft);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": 3}") == -1);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": -1}") == -1);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": \"on\"}") == -1);
    CHECK(pda_mode::stored_from_json("{\"pda_mode\": 1") == -1); // malformed: nothing
    CHECK(pda_mode::stored_from_json("{\"x\": {\"pda_mode\": 1}}") == -1); // x.pda_mode is another key
}

static void test_plan()
{
    const Board land = pda_mode::board_from("800x480", "0", nullptr);
    const Board portrait = pda_mode::board_from("480x800", "90", "1");
    const Board tablet = pda_mode::board_from("1280x800", "0", nullptr);

    // nothing stored: board.conf as it is
    pda_mode::Plan p = pda_mode::plan(portrait, -1, true);
    CHECK(p.env.empty() && p.mode == pda_mode::kLeft && p.canvas == portrait.canvas);
    p = pda_mode::plan(land, -1, true);
    CHECK(p.env.empty() && p.mode == pda_mode::kOff && p.canvas == land.canvas);
    // a stored value where the row is not offered (a config.json from another board): board.conf as it is
    p = pda_mode::plan(land, pda_mode::kLeft, false);
    CHECK(p.env.empty() && p.mode == pda_mode::kOff);
    p = pda_mode::plan(pda_mode::board_from("640x480", nullptr, nullptr), pda_mode::kLeft, true);
    CHECK(p.env.empty());
    // an unknown stored value: board.conf as it is
    p = pda_mode::plan(land, 7, true);
    CHECK(p.env.empty());

    // stored On rotate left on a landscape board: the portrait canvas with the zone
    p = pda_mode::plan(land, pda_mode::kLeft, true);
    CHECK(p.mode == pda_mode::kLeft && same(p.canvas, 480, 800, 90) && p.env.size() == 3);
    CHECK(value_of(p.env, "APPLAUNCH_LOGICAL") == "480x800" && value_of(p.env, "APPLAUNCH_ROTATE") == "90" &&
          value_of(p.env, "APPLAUNCH_PDA") == "1");
    p = pda_mode::plan(land, pda_mode::kRight, true);
    CHECK(value_of(p.env, "APPLAUNCH_LOGICAL") == "480x800" && value_of(p.env, "APPLAUNCH_ROTATE") == "270" &&
          value_of(p.env, "APPLAUNCH_PDA") == "1");
    // stored Off on the PDA install profile: the landscape canvas, no zone (a plain install.sh cannot undo it either)
    p = pda_mode::plan(portrait, pda_mode::kOff, true);
    CHECK(p.mode == pda_mode::kOff && value_of(p.env, "APPLAUNCH_LOGICAL") == "800x480" &&
          value_of(p.env, "APPLAUNCH_ROTATE") == "0" && value_of(p.env, "APPLAUNCH_PDA") == "0");
    p = pda_mode::plan(portrait, pda_mode::kRight, true);
    CHECK(value_of(p.env, "APPLAUNCH_LOGICAL") == "480x800" && value_of(p.env, "APPLAUNCH_ROTATE") == "270" &&
          value_of(p.env, "APPLAUNCH_PDA") == "1");
    // the tablet: 800x1280 rotate 90 / 270 (the study's P0 values for the left side)
    p = pda_mode::plan(tablet, pda_mode::kLeft, true);
    CHECK(value_of(p.env, "APPLAUNCH_LOGICAL") == "800x1280" && value_of(p.env, "APPLAUNCH_ROTATE") == "90" &&
          value_of(p.env, "APPLAUNCH_PDA") == "1");
    p = pda_mode::plan(tablet, pda_mode::kRight, true);
    CHECK(value_of(p.env, "APPLAUNCH_ROTATE") == "270");
    p = pda_mode::plan(tablet, pda_mode::kOff, true);
    CHECK(value_of(p.env, "APPLAUNCH_LOGICAL") == "1280x800" && value_of(p.env, "APPLAUNCH_ROTATE") == "0" &&
          value_of(p.env, "APPLAUNCH_PDA") == "0");
}

/* apply_at_start() in a child: the board's variables, HOME with config.json holding `json` (nullptr: no file); the
 * child checks the environment it left and the startup facts, exit 0 when they match. */
static void run_start(const char *json, const char *logical, const char *rotate, const char *pda,
                      const char *want_logical, const char *want_rotate, const char *want_pda, int want_mode,
                      bool want_applied)
{
    char dir[] = "/tmp/test_pda_mode_XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    const std::string home = dir;
    if (json) {
        CHECK(mkdir((home + "/.config").c_str(), 0755) == 0);
        CHECK(mkdir((home + "/.config/cardputerzero").c_str(), 0755) == 0);
        FILE *fp = std::fopen((home + "/.config/cardputerzero/config.json").c_str(), "w");
        CHECK(fp != nullptr);
        if (fp) {
            std::fputs(json, fp);
            std::fclose(fp);
        }
    }
    const pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        setenv("HOME", home.c_str(), 1);
        setenv("APPLAUNCH_LOGICAL", logical, 1);
        setenv("APPLAUNCH_ROTATE", rotate, 1);
        if (pda) setenv("APPLAUNCH_PDA", pda, 1);
        else unsetenv("APPLAUNCH_PDA");
        setenv("APPLAUNCH_PANEL_MM", "150x93", 1);
        const pda_mode::Startup &s = pda_mode::apply_at_start();
        const char *l = std::getenv("APPLAUNCH_LOGICAL"), *r = std::getenv("APPLAUNCH_ROTATE"),
                   *z = std::getenv("APPLAUNCH_PDA");
        int bad = 0;
        if (!l || std::strcmp(l, want_logical) != 0) bad |= 1;
        if (!r || std::strcmp(r, want_rotate) != 0) bad |= 2;
        if ((want_pda == nullptr) != (z == nullptr) || (z && std::strcmp(z, want_pda) != 0)) bad |= 4;
        if (s.mode != want_mode || s.applied != want_applied) bad |= 8;
        // the board stays as board.conf said (Settings decides with it), and startup() is the same object
        if (!(s.board.canvas.w > 0) || &pda_mode::startup() != &s || &pda_mode::apply_at_start() != &s) bad |= 16;
        _exit(bad);
    }
    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    if (WEXITSTATUS(status) != 0)
        std::fprintf(stderr, "apply_at_start(%s on %s rot %s): mismatch mask %d\n", json ? json : "no file", logical,
                     rotate, WEXITSTATUS(status));
    CHECK(WEXITSTATUS(status) == 0);
    std::remove((home + "/.config/cardputerzero/config.json").c_str());
    rmdir((home + "/.config/cardputerzero").c_str());
    rmdir((home + "/.config").c_str());
    rmdir(dir);
}

static void test_apply_at_start()
{
    // the tablet, nothing stored: board.conf as it is
    run_start(nullptr, "1280x800", "0", nullptr, "1280x800", "0", nullptr, pda_mode::kOff, false);
    run_start("{\"dark_time\": 60}", "1280x800", "0", nullptr, "1280x800", "0", nullptr, pda_mode::kOff, false);
    // stored On rotate left / right: the portrait canvas with the zone
    run_start("{\"pda_mode\": 1}", "1280x800", "0", nullptr, "800x1280", "90", "1", pda_mode::kLeft, true);
    run_start("{\"pda_mode\": 2}", "1280x800", "0", nullptr, "800x1280", "270", "1", pda_mode::kRight, true);
    // the PDA install profile: On by default; stored Off wins over it
    run_start(nullptr, "480x800", "90", "1", "480x800", "90", "1", pda_mode::kLeft, false);
    run_start("{\"pda_mode\": 0}", "480x800", "90", "1", "800x480", "0", "0", pda_mode::kOff, true);
    run_start("{\"pda_mode\": 2}", "480x800", "90", "1", "480x800", "270", "1", pda_mode::kRight, true);
    // a stored value on a board where the row is not offered (square): nothing changes
    run_start("{\"pda_mode\": 1}", "720x720", "0", nullptr, "720x720", "0", nullptr, pda_mode::kOff, false);
    // an unreadable config.json: nothing changes
    run_start("{\"pda_mode\": ", "1280x800", "0", nullptr, "1280x800", "0", nullptr, pda_mode::kOff, false);
}

int main()
{
    test_board_from();
    test_offered();
    test_board_default();
    test_stored();
    test_plan();
    test_apply_at_start();
    if (failures) {
        std::fprintf(stderr, "test_pda_mode: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("test_pda_mode: ok\n");
    return 0;
}
