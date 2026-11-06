/*
 * SPDX-License-Identifier: MIT
 *
 * Private layout of a map image (src/map): a small block in front of the public header, so the RAM cache finds its
 * entry from the image pointer it handed out and refuses images it did not allocate.
 */
#ifndef CP0_MAP_IMAGE_PRIV_H
#define CP0_MAP_IMAGE_PRIV_H

#include "cp0_map_internal.h"

#include <stddef.h>

#define CP0_MAP_IMAGE_MAGIC 0x4d415049u /* "MAPI" */

typedef struct {
    uint32_t magic;
    void *owner;                 /* the cache entry holding it, NULL when not cached */
    cp0_map_image_t img;
} cp0_map_image_block_t;

/* The block of an image from cp0_map_image_new(), or NULL (NULL image, or not one of ours). */
CP0_MAP_HIDDEN cp0_map_image_block_t *cp0_map__image_block(const cp0_map_image_t *img);

#endif /* CP0_MAP_IMAGE_PRIV_H */
