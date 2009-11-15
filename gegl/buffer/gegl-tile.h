/* This file is part of GEGL.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006,2007 Øyvind Kolås <pippin@gimp.org>
 */

#ifndef __GEGL_TILE_H__
#define __GEGL_TILE_H__

#include <glib-object.h>

#include "gegl-buffer-types.h"
#include "gegl-gpu-types.h"

#define GEGL_TYPE_TILE            (gegl_tile_get_type ())
#define GEGL_TILE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GEGL_TYPE_TILE, GeglTile))
#define GEGL_TILE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  GEGL_TYPE_TILE, GeglTileClass))
#define GEGL_IS_TILE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GEGL_TYPE_TILE))
#define GEGL_IS_TILE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  GEGL_TYPE_TILE))
#define GEGL_TILE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  GEGL_TYPE_TILE, GeglTileClass))

typedef enum
{
  GEGL_TILE_LOCK_NONE,
  GEGL_TILE_LOCK_READ          =  1,
  GEGL_TILE_LOCK_WRITE         = (1 << 1),
  GEGL_TILE_LOCK_READWRITE     = GEGL_TILE_LOCK_READ
                                 | GEGL_TILE_LOCK_WRITE,

  GEGL_TILE_LOCK_GPU_READ      = (1 << 2),
  GEGL_TILE_LOCK_GPU_WRITE     = (1 << 3),
  GEGL_TILE_LOCK_GPU_READWRITE = GEGL_TILE_LOCK_GPU_READ
                                 | GEGL_TILE_LOCK_GPU_WRITE,

  GEGL_TILE_LOCK_ALL_READ      = GEGL_TILE_LOCK_READ
                                 | GEGL_TILE_LOCK_GPU_READ,
  GEGL_TILE_LOCK_ALL_WRITE     = GEGL_TILE_LOCK_WRITE
                                 | GEGL_TILE_LOCK_GPU_WRITE,

  GEGL_TILE_LOCK_ALL           = GEGL_TILE_LOCK_ALL_READ
                                 | GEGL_TILE_LOCK_ALL_WRITE
} GeglTileLockMode;

/* the instance size of a GeglTile is a bit large, and should if possible be
 * trimmed down
 */
struct _GeglTile
{
  GObject          parent_instance;

  guchar          *data;         /* actual pixel data for tile,
                                  * a linear buffer
                                  */
  gint             size;         /* The size of the linear buffer */
#if HAVE_GPU
  GeglGpuTexture  *gpu_data;     /* pixel data for tile, stored in the GPU */
#endif

  GeglTileStorage *tile_storage; /* the buffer from which this tile was
                                  * retrieved, needed for the tile to be able
                                  * to store itself back (for instance when it
                                  * is unreffed for the last time)
                                  */
  gint             x, y, z;

  guint            rev;          /* this tile's revision */
#if HAVE_GPU
  guint            gpu_rev;      /* this tile's GPU data revision */ 
#endif
  guint            stored_rev;   /* what revision was the tile when it was
                                  * committed to the tile_storage? (currently
                                  * set to 1 when loaded from disk)
                                  */

  guint            read_locks;   /* number of times the tile is read-locked,
                                  * should in theory just have the values 0/1,
                                  * note that we might want to have shared
                                  * reads though (not yet implemented)
                                  */
  gchar            write_locks;  /* number of times the tile is write-locked,
                                  * should in theory just have the values 0/1
                                  */
  GeglTileLockMode lock_mode;

#if ENABLE_MP
  GMutex          *mutex;
#endif

  /* the shared list is a doubly linked circular list */
  GeglTile        *next_shared;
  GeglTile        *prev_shared;

  void (*destroy_notify) (gpointer        pixels,
#if HAVE_GPU
                          GeglGpuTexture *gpu_data,
#endif
                          gpointer        data);

  gpointer         destroy_notify_data;
};

struct _GeglTileClass
{
  GObjectClass parent_class;
};

GType           gegl_tile_get_type     (void) G_GNUC_CONST;

GeglTile       *gegl_tile_new          (gint width,
                                        gint height,
                                        const Babl *format);

Babl           *gegl_tile_get_format   (GeglTile *tile);
gint            gegl_tile_get_width    (GeglTile *tile);
gint            gegl_tile_get_height   (GeglTile *tile);

/* lock a tile for access, this would allow access to buffers
 * later gotten with get_data() or get_gpu_data()
 */
void            gegl_tile_lock         (GeglTile *tile,
                                        GeglTileLockMode lock_mode);

/* get a pointer to the linear buffer of the tile */
guchar         *gegl_tile_get_data     (GeglTile *tile);

#if HAVE_GPU
/* get a pointer to the GPU data of the tile */
GeglGpuTexture *gegl_tile_get_gpu_data (GeglTile *tile);
#endif

/* unlock the tile notifying the tile that we're done accessing
 * the data
 */
void            gegl_tile_unlock       (GeglTile *tile);

gboolean        gegl_tile_is_stored    (GeglTile *tile);
gboolean        gegl_tile_store        (GeglTile *tile);
void            gegl_tile_void         (GeglTile *tile);
GeglTile       *gegl_tile_dup          (GeglTile *tile);

/* computes the positive integer remainder (also for negative dividends) */
#define GEGL_REMAINDER(dividend, divisor) \
                   (((dividend) < 0) ? \
                     (divisor) - 1 - ((-((dividend) + 1)) % (divisor)) : \
                     (dividend) % (divisor))

#define gegl_tile_offset(coordinate, stride) \
                   GEGL_REMAINDER ((coordinate), (stride))

/* helper function to compute tile indices and offsets for coordinates
 * based on a tile stride (tile_width or tile_height)
 */
#define gegl_tile_index(coordinate, stride) \
                   (((coordinate) >= 0) ? \
                     (coordinate) / (stride) : \
                     ((((coordinate) + 1) / (stride)) - 1))

/* utility low-level functions used by undo system */
void            gegl_tile_swp          (GeglTile *a,
                                        GeglTile *b);
void            gegl_tile_cpy          (GeglTile *src,
                                        GeglTile *dst);

#endif /* __GEGL_TILE_H__ */
