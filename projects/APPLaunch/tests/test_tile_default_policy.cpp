// SPDX-License-Identifier: MIT
//
// Board-profile tile defaults (main/ui/model/tile_default_policy.hpp): APPLAUNCH_HIDE_TILES=app_Store (the PC profile,
// report 028 X13) starts the Store tile hidden; any other tile, an empty or missing list keep the default "shown".

#include "../main/ui/model/tile_default_policy.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                              \
        }                                                                              \
    } while (0)

int main()
{
    using tile_default_policy::default_enabled;
    using tile_default_policy::hidden_by_default;

    CHECK(default_enabled("app_Store", "app_Store") == 0);
    CHECK(default_enabled("app_Python", "app_Store") == 1);
    CHECK(default_enabled("app_Store", nullptr) == 1);  // no profile (the deck): unchanged
    CHECK(default_enabled("app_Store", "") == 1);
    CHECK(default_enabled("", "app_Store,") == 1);      // a tile without a key is never hidden
    CHECK(hidden_by_default("app_Game", "app_Store, app_Game ,app_Tank"));
    CHECK(hidden_by_default("app_Tank", "app_Store,app_Tank"));
    CHECK(!hidden_by_default("app_Sto", "app_Store"));  // whole entries only
    CHECK(!hidden_by_default("app_Store", "app_Store_x,xapp_Store"));
    CHECK(hidden_by_default("app_Store", ",,app_Store,,"));
    std::printf("test_tile_default_policy: ok\n");
    return 0;
}
