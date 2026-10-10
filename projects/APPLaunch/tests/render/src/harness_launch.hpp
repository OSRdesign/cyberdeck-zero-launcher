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
};

/* Replaces the home grid's app list (a new Launch object, attached to native_ui). */
void set_apps(const std::vector<AppSpec> &apps);
Launch *launcher();
int app_index(const std::string &name);

} // namespace harness
