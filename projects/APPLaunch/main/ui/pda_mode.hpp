/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Screen > PDA mode (report 043, phase S7b): the portrait handwriting-band profile as a stored choice, Off /
 * On rotate left / On rotate right, applied when the launcher starts (a change restarts it,
 * settings_session_model.hpp).
 * PDA mode is a portrait canvas with the writing zone (APPLAUNCH_PDA=1, cp0_zone.h); landscape PDA is not offered.
 *
 * The board: the geometry of its profile (board.conf, written by install.sh), APPLAUNCH_LOGICAL=WxH and APPLAUNCH_ROTATE.
 * From its canvas the three canvases of the setting:
 *   Off               the landscape canvas: a landscape board's own; for a portrait board (the PDA install profile
 *                     pi3a-wsdsi43-portrait: 480x800 rotate 90 on the 800x480 DSI framebuffer) the panel's landscape as
 *                     the landscape install profiles have it: rotate 0 on a landscape framebuffer (pi3a-wsdsi43:
 *                     800x480 rotate 0), 90 on a portrait one (pi5-hyperpixel4: 800x480 rotate 90);
 *   On rotate left    that landscape canvas turned to portrait with the device turned a quarter to the left
 *                     (counter-clockwise): rotate + 90, the landscape panel's right edge at the top (the install
 *                     profile's 480x800 rotate 90; the tablet's 1280x800 rotate 0 -> 800x1280 rotate 90);
 *   On rotate right   the device turned a quarter to the right: rotate + 270, the left edge at the top (480x800 rotate
 *                     270; the tablet's 800x1280 rotate 270).
 * Both sides are the display manager's CPU output stage (cp0_fb_output.h: every rotation, both depths) and the touch
 * follows it (APPLAUNCH_TOUCH_ORIENT=buffer turns the raw axes back through APPLAUNCH_ROTATE; the legacy mapping does
 * too). A landscape app run turned (app_launch_env::turned_rotation(), the device turned clockwise back from portrait)
 * gets the landscape canvas at rotate + 0 on the left side and at rotate + 180 on the right side, which the apps' own
 * output stage (the toolkit of every app since v0.8.5) and viz1090's draw as well.
 * Offered (the row is shown) only when the board's geometry is known (both variables set and valid: the deck has no
 * board profile, its unit sets the rotation only), not square (a square panel has no portrait: turning it changes nothing), its short side is at least
 * kMinShortSide px and the writing zone fits the portrait canvas (cp0_zone_layout(): a third of the panel, the page
 * keeping CP0_ZONE_APP_KEEP_PX above the band; the density from APPLAUNCH_PANEL_MM as the display manager reads it).
 * Every offered board offers the three values.
 *
 * The choice is stored as config "pda_mode" (0 Off, 1 On rotate left, 2 On rotate right) in
 * ~/.config/cardputerzero/config.json, which install.sh never touches: when it is stored it wins over board.conf (a
 * plain install.sh that writes the landscape profile again changes nothing visible); when nothing is stored the
 * board.conf values stay as they are (pi3a-wsdsi43-portrait: PDA by default, rotate left). apply_at_start() runs in
 * main() before the display opens: it reads the stored value from the file (the config service starts later) and sets
 * APPLAUNCH_LOGICAL, APPLAUNCH_ROTATE and APPLAUNCH_PDA, which the display manager and every app the launcher starts
 * read (a fullscreen app then sees the panel the launcher draws; a turned app gets its own per launch; app_launch_env
 * exports APPLAUNCH_PDA=0 to every app: the band is the launcher's).
 *
 * Pure except apply_at_start() / startup() (environment, the config file) and zone_fits() (the layout service's and
 * the writing zone's pure C). Unit-tested by tests/test_pda_mode.cpp.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pda_mode {

constexpr const char *kConfigKey = "pda_mode";
constexpr int kOff = 0;
constexpr int kLeft = 1;  /* On rotate left: the landscape canvas + 90 */
constexpr int kRight = 2; /* On rotate right: the landscape canvas + 270 */
/* the short side of the panel a portrait PDA canvas needs (480x800 is the reference) */
constexpr int kMinShortSide = 480;

struct Canvas {
    int w = 0, h = 0, rot = 0; /* logical size, clockwise pre-rotation into the framebuffer */
};

inline bool operator==(const Canvas &a, const Canvas &b)
{
    return a.w == b.w && a.h == b.h && a.rot == b.rot;
}

struct Board {
    bool known = false; /* APPLAUNCH_LOGICAL a WxH and APPLAUNCH_ROTATE 0|90|180|270 */
    Canvas canvas;      /* board.conf's */
    bool pda = false;   /* board.conf's APPLAUNCH_PDA is 1 */
    Canvas landscape;   /* Off (square: the canvas) */
    Canvas left, right; /* On rotate left / right (square: the canvas) */
};

/* From the board profile's variables (null or "" = unset). */
Board board_from(const char *logical, const char *rotate, const char *pda);

/* The writing zone fits this portrait canvas (cp0_zone_layout() on its metrics, the density from panel_mm or the
 * default). */
bool zone_fits(const Canvas &portrait, const char *panel_mm);

using ZoneFits = bool (*)(const Canvas &portrait, const char *panel_mm);

/* The row is shown (see above). */
bool offered(const Board &board, const char *panel_mm, ZoneFits fits = zone_fits);

/* What runs when nothing is stored: a portrait board.conf with APPLAUNCH_PDA=1 is its side (kLeft or kRight), anything
 * else kOff. */
int board_default(const Board &board);

/* The canvas of a value (kOff: the landscape one). */
const Canvas &canvas_of(const Board &board, int mode);

/* The values Settings lists, in its order (Off, On rotate left, On rotate right); empty where not offered. */
std::vector<int> choices(const Board &board, bool is_offered);

/* "Off", "On rotate left", "On rotate right" */
const char *label(int mode);

/* The stored value in config.json's text: kOff, kLeft, kRight, or -1 (absent, another value, unreadable). */
int stored_from_json(const std::string &json);

using Vars = std::vector<std::pair<std::string, std::string>>;

struct Plan {
    int mode = kOff;    /* what runs */
    Canvas canvas;      /* its canvas (the board's when nothing changes) */
    Vars env;           /* APPLAUNCH_LOGICAL, APPLAUNCH_ROTATE, APPLAUNCH_PDA to set; empty = board.conf as it is */
};

/* stored (-1 = nothing) applied to the board: only where offered; otherwise board.conf as it is. */
Plan plan(const Board &board, int stored, bool is_offered);

/* What this launcher started with. */
struct Startup {
    Board board;
    bool offered = false;
    int stored = -1;
    int mode = kOff;
    bool applied = false; /* the environment was changed by apply_at_start() */
};

/* main(), before the display opens: the board from the environment, the stored value from the config file, the
 * environment set by plan(); logged on one "[pda-mode]" line. Once (later calls return the first result). */
const Startup &apply_at_start();

/* apply_at_start()'s result; without it (the render harness: the display is already open) the environment taken as the
 * board's and nothing stored. */
const Startup &startup();

/* ~/.config/cardputerzero/config.json (HOME, else /root), the config service's file. */
std::string config_path();

} // namespace pda_mode
