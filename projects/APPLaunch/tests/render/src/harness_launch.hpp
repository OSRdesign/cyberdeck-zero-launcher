/* SPDX-License-Identifier: MIT
 *
 * Render harness: the scene's app list (home tiles) and the fake Launch object.
 */
#pragma once

#include <string>
#include <vector>

class Launch;

namespace harness {

struct AppSpec {
    std::string name;
    std::string icon;  /* path as the launcher hands it to lv_image_set_src ("A:/...png") */
    std::string kind;  /* "settings", "calculator" or "tile" (no page behind it) */
    /* An app without an icon (scene "Name=none" / "Name=stale"): icon is "", as the desktop loader leaves it for a
     * .desktop entry with no Icon= line. The bytes after the empty string's terminator are what a recycled heap chunk
     * left there, so the harness sets them: zeros (none), or an image header a real run once found there (stale). */
    enum class Empty { No, Zeroed, Stale } empty = Empty::No;
};

/* Replaces the home grid's app list (a new Launch object, attached to native_ui). */
void set_apps(const std::vector<AppSpec> &apps);
Launch *launcher();
int app_index(const std::string &name);

} // namespace harness
