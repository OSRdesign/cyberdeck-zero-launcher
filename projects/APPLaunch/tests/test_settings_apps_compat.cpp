/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > Apps, apps for the board's screen: the pure filter (registry min_size / portrait_min_size against the
 * logical screen, the launcher's own rule for an installed app), the registry reader (both field shapes, malformed
 * values, the backend's cache record) and the Available list (hidden, counted, re-filtered when the screen changes).
 */

#include "../main/ui/settings/apps_backend.hpp"
#include "../main/ui/settings/settings_apps_compat.hpp"
#include "../main/ui/settings/settings_apps_model.hpp"
#include "../main/ui/settings/settings_apps_registry_needs.hpp"

#include <cassert>
#include <string>
#include <vector>

using namespace settings_apps;

static void filter()
{
    // no field: compatible everywhere
    assert(app_fits_screen("", 480, 320));
    assert(app_fits_screen("", 480, 800));
    assert(app_fits_screen("", 160, 80));
    // landscape 480x320 hides a 640x480 app, 640x480 shows it, exactly equal fits, one pixel short does not
    assert(!app_fits_screen("640x480", 480, 320));
    assert(app_fits_screen("640x480", 640, 480));
    assert(app_fits_screen("480x320", 480, 320));
    assert(!app_fits_screen("480x321", 480, 320));
    assert(!app_fits_screen("481x320", 480, 320));
    assert(app_fits_screen("640x480", 1280, 800)); // tablet
    // portrait boards hide nothing for their orientation: judged as the turned landscape canvas (480x800 = 800x480)
    assert(app_fits_screen("640x480", 480, 800));
    assert(app_fits_screen("800x480", 480, 800)); // exactly equal
    assert(!app_fits_screen("801x480", 480, 800));
    assert(!app_fits_screen("640x480", 320, 480)); // turned canvas 480x320: too small, as on a landscape 480x320
    assert(app_fits_screen("480x320", 320, 480));
    assert(app_fits_screen("640x480", 480, 640)); // PDA 480x640 = 640x480
    assert(app_fits_screen("640x480", 640, 640)); // square: as it is
    // malformed: compatible (like an unparsable X-MinSize)
    assert(app_fits_screen("big", 480, 320));
    assert(app_fits_screen("640", 480, 320));
    assert(app_fits_screen("0x0", 480, 320));
    assert(app_fits_screen("640x", 480, 320));
    assert(app_fits_screen("-1x5", 480, 320));
    assert(app_fits_screen(" 640 X 480 ", 640, 480)); // same syntax as X-MinSize
    // unknown screen: everything fits
    assert(app_fits_screen("640x480", 0, 0));
    assert(size_value_valid("640x480") && !size_value_valid("") && !size_value_valid("x"));
}

static void facts()
{
    ScreenFacts f = screen_facts("640x480", "");
    assert(f.screen == "Landscape" && f.minimum == "640x480");
    f = screen_facts("640x480", "480x640");
    assert(f.screen == "Landscape and portrait" && f.minimum == "640x480");
    f = screen_facts(" 640 x 480", "");
    assert(f.minimum == "640x480");
    f = screen_facts("", "");
    assert(f.screen.empty() && f.minimum.empty());
    f = screen_facts("junk", "junk");
    assert(f.screen.empty() && f.minimum.empty());
    f = screen_facts("junk", "480x640");
    assert(f.screen == "Landscape and portrait" && f.minimum.empty());
    f = screen_facts("640x480", "junk"); // an unreadable portrait size says nothing about portrait
    assert(f.screen == "Landscape" && f.minimum == "640x480");
}

static void reader()
{
    const std::string registry = R"({"url":"u","status":"ok","index":{"apps":[
        {"uuid":"a","title":"A","min_size":"640x480","portrait_min_size":"480x640"},
        {"uuid":"b","title":"B"},
        {"share_code":"c","title":"C","min_size":"480x320"},
        {"uuid":"d","min_size":"junk"},
        {"uuid":"e","min_size":"640x480","portrait":true},
        {"uuid":"f","min_size":"640x480","portrait":"480x640","app":{"applaunch":{"desktop_entry":"applications/f.desktop"}}},
        {"min_size":"640x480"},
        7]}})";
    const auto needs = parse_registry_needs(registry);
    assert(needs.size() == 6);
    assert(needs.at("a").min_size == "640x480" && needs.at("a").portrait_min_size == "480x640");
    assert(needs.at("a").desktop == "applications/a.desktop"); // title slug
    assert(needs.at("b").min_size.empty() && needs.at("b").portrait_min_size.empty());
    assert(needs.at("c").min_size == "480x320" && needs.at("c").portrait_min_size.empty());
    assert(needs.at("c").desktop == "applications/c.desktop"); // share_code
    assert(needs.at("d").min_size == "junk"); // kept; the filter takes it as absent
    assert(needs.at("e").portrait_min_size.empty()); // a flag carries no size
    assert(needs.at("f").portrait_min_size == "480x640");
    assert(needs.at("f").desktop == "applications/f.desktop");
    assert(needs.count("") == 0);
    // not JSON, or not a registry: nothing
    assert(parse_registry_needs("").empty());
    assert(parse_registry_needs("{broken").empty());
    assert(parse_registry_needs("[1,2]").empty());
    assert(parse_registry_needs(R"({"index":{"apps":"x"}})").empty());
    // a registry file itself (apps at the top level)
    assert(parse_registry_needs(R"({"apps":[{"uuid":"z","min_size":"1x1"}]})").count("z") == 1);
    assert(read_cache_needs("/nonexistent/applaunch-tests").empty());
    // an installed app's .desktop, through the launcher's own parser
    ScreenNeeds own;
    assert(parse_desktop_needs("[Desktop Entry]\nName=X\nExec=/bin/x\nX-MinSize=640x480\nX-Portrait=480x640\n", own));
    assert(own.min_size == "640x480" && own.portrait_min_size == "480x640");
    assert(parse_desktop_needs("[Desktop Entry]\nName=X\nExec=/bin/x\nX-MinSize=junk\n", own));
    assert(own.min_size.empty() && own.portrait_min_size.empty());
    assert(!parse_desktop_needs("nothing", own));
    assert(!read_desktop_needs("/etc/passwd", own) && !read_desktop_needs("../x.desktop", own) &&
           !read_desktop_needs("", own));
}

static apps_backend::App app(const char *id, const char *min, const char *portrait, bool installed = false)
{
    apps_backend::App entry;
    entry.id = id;
    entry.title = id;
    entry.version = "1.0.0";
    entry.min_size = min;
    entry.portrait_min_size = portrait;
    entry.installed = installed;
    if (installed) entry.installed_version = "1.0.0";
    return entry;
}

static int app_rows(const settings_apps::Model &model)
{
    int n = 0;
    for (const settings_apps::Row &row : model.rows())
        if (row.type == settings_apps::RowType::App) ++n;
    return n;
}

static void list()
{
    using namespace settings_apps;
    Model model;
    apps_backend::Source source;
    source.url = "u";
    source.name = "s";
    model.set_lists({app("big", "640x480", "480x640"), app("mid", "480x320", ""), app("any", "", ""),
                     app("tall", "800x600", "480x640"), app("bigdone", "640x480", "", true)},
                    {source});
    // no screen known: everything listed
    model.set_level(Level::Available);
    assert(app_rows(model) == 4);
    assert(model.available_count() == 4);
    // zero8: 480x320 landscape hides the 640x480 and 800x600 apps; no hint row, installed apps unaffected
    assert(model.set_screen(480, 320));
    assert(!model.set_screen(480, 320));
    assert(app_rows(model) == 2 && model.rows().size() == 2);
    assert(model.available_count() == 2);
    assert(model.installed_count() == 1);
    model.set_level(Level::Installed);
    assert(app_rows(model) == 1); // bigdone stays listed
    model.set_level(Level::Available);
    // deck 640x480: 800x600 hidden only
    assert(model.set_screen(640, 480));
    assert(app_rows(model) == 3);
    // tablet: all shown
    model.set_screen(1280, 800);
    assert(app_rows(model) == 4);
    // orientation change (PDA mode): a portrait panel is judged as its turned landscape canvas
    model.set_screen(480, 800); // 800x480: big and mid fit, tall (800x600) does not
    assert(app_rows(model) == 3);
    model.set_screen(480, 640); // 640x480
    assert(app_rows(model) == 3);
    model.set_screen(320, 480); // 480x320
    assert(app_rows(model) == 2);
    model.set_screen(480, 320);
    assert(app_rows(model) == 2);
    // everything hidden: the normal empty-list text, nothing else
    model.set_lists({app("big", "640x480", "")}, {source});
    const auto rows = model.rows();
    assert(rows.size() == 1 && rows[0].type == RowType::Placeholder && rows[0].label == "Nothing to install");
    // an open Available app's list closes when the screen change hides the app
    model.set_lists({app("big", "640x480", ""), app("any", "", "")}, {source});
    model.set_screen(640, 480);
    assert(model.open_actions(0));
    assert(model.actions_valid());
    model.set_screen(480, 320);
    assert(!model.actions_valid());
    // the root count follows
    model.close_actions();
    model.set_level(Level::Root);
    assert(model.rows()[1].label == "Available (1)");
}

static void app_page()
{
    using namespace settings_apps;
    Model model;
    apps_backend::Source source;
    source.url = "u";
    source.name = "s";
    apps_backend::App described = app("both", "640x480", "480x640");
    described.description = "A game.";
    apps_backend::App landscape = app("land", "480x320", "");
    landscape.description = "Landscape only.";
    model.set_lists({app("none", "", ""), described, landscape, app("inst", "640x480", "480x640", true)}, {source});
    model.set_level(Level::Available);
    auto page = [&](const char *id) {
        for (int i = 0; i < static_cast<int>(model.rows().size()); ++i)
            if (model.rows()[static_cast<std::size_t>(i)].key == id) {
                assert(model.open_actions(i));
                return model.rows();
            }
        assert(false);
        return std::vector<Row>();
    };
    // both: Screen and Minimum size before the Description, then Install, Cancel
    auto rows = page("both");
    assert(rows.size() == 5);
    assert(rows[0].type == RowType::Info && rows[0].label == "Screen" && rows[0].value == "Landscape and portrait");
    assert(rows[1].type == RowType::Info && rows[1].label == "Minimum size" && rows[1].value == "640x480");
    assert(rows[2].type == RowType::Description && rows[3].type == RowType::Install && rows[4].type == RowType::Cancel);
    assert(rows[0].tone == Tone::Muted && rows[1].tone == Tone::Muted);
    assert(model.first_actionable_row() == 2);
    // the info rows do nothing
    assert(model.activate(0).kind == ActionKind::None && model.activate(1).kind == ActionKind::None);
    assert(!model.ask_remove(0));
    model.close_actions();
    rows = page("land");
    assert(rows[0].value == "Landscape" && rows[1].value == "480x320" && rows.size() == 5);
    model.close_actions();
    // no fields: no Screen / Minimum size rows
    rows = page("none");
    assert(rows.size() == 2 && rows[0].type == RowType::Install && model.first_actionable_row() == 0);
    model.close_actions();
    // the installed app's page carries them too
    model.set_level(Level::Installed);
    rows = page("inst");
    assert(rows[0].type == RowType::Info && rows[0].value == "Landscape and portrait");
    assert(rows.back().type == RowType::Cancel);
    // the 500-character rule is the description's alone: the rows are not part of its text
    model.close_actions();
    assert(cap_description(std::string(600, 'a')).size() == 500);
}

int main()
{
    filter();
    facts();
    app_page();
    reader();
    list();
    return 0;
}
