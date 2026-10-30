/*
 * SPDX-License-Identifier: MIT
 *
 * The launcher's side of the home grid's user order (model/home_order.hpp, study 046): the config service and the app
 * registry. LVGL thread (the config service is the launcher's own).
 *
 *   launcher_home_order_apply   Launch::applications_load(), after today's three steps: only when a user order is
 *                               stored, the tiles go in its order (tiles without an id count as unknown: today's
 *                               order, after the known ones). No stored order: the list is not touched.
 *   launcher_home_order_save    after a move (P2): `apps` is the grid in its new order. Stores every registered tile
 *                               (launcher_app_registry_entries(), hidden ones included) in one SetManyAndSave. False:
 *                               not saved (the store is full): the caller tells the user.
 *   launcher_home_order_stored  a user order exists (Settings > Launcher "Reset app order").
 *   launcher_home_order_reset   clears every home_order* key: the next load gives today's grid. The caller reloads the
 *                               grid (launcher_app_registry_notify_changed()). False: the store refused the write.
 */
#pragma once

#include <list>

struct app;

void launcher_home_order_apply(std::list<app> &apps);
bool launcher_home_order_save(const std::list<app> &apps);
bool launcher_home_order_stored();
bool launcher_home_order_reset();
