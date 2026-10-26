/*
 * SPDX-License-Identifier: MIT
 *
 * Settings > System > Strokes training (PDA phase S6, report 042): a native page, on the PDA profile only (the writing
 * zone exists: launcher_zone::active()). It keeps the writing zone's band shown and takes its strokes (a capture: nothing
 * is typed), shows the glyph to write with its shape as a guide (light ink, a dot where it starts), judges each attempt
 * (recognised / refused / read as another glyph, a short coloured flash, the attempts kept as thumbnails), turns the
 * attempts into the user's own templates in the user overlay (strokes_train.h) and reloads the recogniser; Back, Skip,
 * Redo this glyph, Reset all personal templates (confirmed), Finish (a quick re-test, then the summary: recognised
 * before and after). Every attempt goes to the capture log next to the overlay (strokes-training.txt, the corpus format
 * of tools/strokes: strokes-log / strokes-replay read it on a PC).
 */
#pragma once

#include "lvgl_components.hpp"
#include "settings_native_panel.hpp"

#include <functional>
#include <memory>

// The entry's factory: a marker for settings_native_page() (SettingsNativePage::StrokesTraining); the native host opens
// the page with settings_strokes_training_panel_create(). It builds nothing for the legacy renderer.
std::unique_ptr<DComponens::LvglComponensBase> settings_strokes_training_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> back_callback);

std::unique_ptr<SettingsNativePanel> settings_strokes_training_panel_create(const SettingsNativePanelContext &context);

namespace settings_strokes {

// Appends "Strokes training" to the System section when the writing zone exists. Native Settings only.
void append_entry(Tree &tree, const NodeIter &system);

} // namespace settings_strokes
