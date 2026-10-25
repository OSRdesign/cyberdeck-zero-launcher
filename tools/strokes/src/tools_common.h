/*
 * SPDX-License-Identifier: MIT
 *
 * Helpers shared by the command-line tools of tools/strokes: table loading and "key=value" settings.
 */
#ifndef STROKES_TOOLS_COMMON_H
#define STROKES_TOOLS_COMMON_H

#include "strokes.h"

#include <stdio.h>

/* The shipped table path: $STROKES_TABLE, ./share/glyphs.txt, or <dir of argv0>/../../share/glyphs.txt. */
const char *tools_default_table(const char *argv0, char *buf, size_t size);
/* Loads the table and the overlays (in order); prints the error and returns NULL on failure. */
strokes_table *tools_load_table(const char *table, const char **overlays, int n_overlays);
/* "key=value" for a strokes_config field (README "Tunables"). Returns -1 when unknown or bad. */
int tools_cfg_set(strokes_config *c, const char *kv);
void tools_cfg_describe(const strokes_config *c, char *buf, size_t size);
/* The set of the glyph a corpus label names, preferring the box's own set, then "any", punct, ext; -1 = unknown. */
int tools_label_set(const strokes_table *t, const char *id, int box);

#endif
